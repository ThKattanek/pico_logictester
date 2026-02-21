#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "displaylib_16/displaylib_16_Font.hpp"
#include "displaylib_16/st7735.hpp"

// Display GPIO and SPI configuration
// SPI0 
#define TFT_CS_PIN    2     // Chip Select pin
#define TFT_DC_PIN    3     // Data/Command pin
#define TFT_RST_PIN   4     // Reset pin
#define TFT_SDIN_PIN  19    // Master Out Slave In pin
#define TFT_SCLK_PIN   18   // Serial Clock pin   

#define TFT_WIDTH     128   // Display width in pixels
#define TFT_HEIGHT    160   // Display height in pixels

// Power management pins
#define SYSTEM_POWER_PIN 0    // Controls PNP transistor to keep system powered ()
#define POWER_BUTTON_PIN 1    // Power button input

// Battery monitoring
#define BATTERY_ADC_PIN 27    // ADC1 - Battery voltage measurement
#define BATTERY_ADC_CHANNEL 1 // ADC channel 1 (GPIO 27)

// Logic Input GPIO pins
#define LOGIC_PIN_0 6       // Logic Input Pin 0
#define LOGIC_PIN_1 7       // Logic Input Pin 1
#define LOGIC_PIN_2 8       // Logic Input Pin 2
#define LOGIC_PIN_3 9       // Logic Input Pin 3
#define LOGIC_PIN_4 10      // Logic Input Pin 4
#define LOGIC_PIN_5 11      // Logic Input Pin 5
#define LOGIC_PIN_6 12      // Logic Input Pin 6
#define LOGIC_PIN_7 13      // Logic Input Pin 7

#define LOGIC_PIN_8 14      // Logic Input Pin 8
#define LOGIC_PIN_9 15      // Logic Input Pin 9
#define LOGIC_PIN_10 16      // Logic Input Pin 10
#define LOGIC_PIN_11 17      // Logic Input Pin 11
#define LOGIC_PIN_12 20     // Logic Input Pin 12
#define LOGIC_PIN_13 21     // Logic Input Pin 13
#define LOGIC_PIN_14 22     // Logic Input Pin 14
#define LOGIC_PIN_15 26     // Logic Input Pin 15

uint8_t LogicGpioPins[16] = {
    LOGIC_PIN_0,
    LOGIC_PIN_1,
    LOGIC_PIN_2,
    LOGIC_PIN_3,
    LOGIC_PIN_4,
    LOGIC_PIN_5,
    LOGIC_PIN_6,
    LOGIC_PIN_7,
    LOGIC_PIN_8,
    LOGIC_PIN_9,
    LOGIC_PIN_10,
    LOGIC_PIN_11,
    LOGIC_PIN_12,
    LOGIC_PIN_13,
    LOGIC_PIN_14,
    LOGIC_PIN_15    
};

ST7735_TFT tft_display;

// Cache for previous logic states to avoid unnecessary display updates
uint16_t prev_logic_state = 0xFFFF; // Initialize to invalid state to force initial update

// Power management variables
volatile bool power_button_pressed = false;
uint32_t power_button_press_time = 0;
const uint32_t POWER_OFF_HOLD_TIME = 2000; // Hold power button for 2 seconds to shut down

// Battery monitoring variables
float battery_voltage = 0.0f;
uint32_t last_battery_check = 0;
uint32_t last_battery_display_update = 0;
const uint32_t BATTERY_CHECK_INTERVAL = 100; // Check battery every 1/10 seconds
const uint32_t BATTERY_DISPLAY_UPDATE_INTERVAL = 1000; // Update display every 1 second
const float BATTERY_LOW_THRESHOLD = 3.7f;     // Low battery warning at 3.7V
const float BATTERY_CRITICAL_THRESHOLD = 3.5f; // Critical battery at 3.5V
const float ADC_CONVERSION_FACTOR = 3.3f / (1 << 12); // 3.3V reference, 12-bit ADC
const float VOLTAGE_DIVIDER_FACTOR = 2.0f;    // Adjust based on your voltage divider

// Moving average for battery voltage (50 measurements)
const int BATTERY_SAMPLES = 50;
float battery_readings[BATTERY_SAMPLES];
int battery_sample_index = 0;
bool battery_samples_filled = false;
float battery_voltage_average = 0.0f;

void PowerButtonCallback(uint gpio, uint32_t events)
{
    if (gpio == POWER_BUTTON_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        power_button_pressed = true;
        power_button_press_time = to_ms_since_boot(get_absolute_time());
    }
}

void InitPowerManagement()
{
    // Initialize power control pin - keep power on (HIGH for PNP transistor)
    gpio_init(SYSTEM_POWER_PIN);
    gpio_set_dir(SYSTEM_POWER_PIN, GPIO_OUT);
    gpio_put(SYSTEM_POWER_PIN, 1); // HIGH = PNP conducts = MOSFET conducts = power on
    
    // Initialize power button pin with pull-up
    gpio_init(POWER_BUTTON_PIN);
    gpio_set_dir(POWER_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(POWER_BUTTON_PIN);
    
    // Setup interrupt for power button
    gpio_set_irq_enabled_with_callback(POWER_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &PowerButtonCallback);
}

bool CheckPowerButton()
{
    if (power_button_pressed) {
        if (!gpio_get(POWER_BUTTON_PIN)) { // Button still pressed
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            if (current_time - power_button_press_time >= POWER_OFF_HOLD_TIME) {
                return true; // Time to power off
            }
        } else {
            // Button released before timeout
            power_button_pressed = false;
        }
    }
    return false;
}

void InitBatteryMonitoring()
{
    // Initialize ADC for battery monitoring
    adc_init();
    adc_gpio_init(BATTERY_ADC_PIN);
    adc_select_input(BATTERY_ADC_CHANNEL);
}

float ReadBatteryVoltage()
{
    adc_select_input(BATTERY_ADC_CHANNEL);
    uint16_t adc_raw = adc_read();
    float voltage = adc_raw * ADC_CONVERSION_FACTOR * VOLTAGE_DIVIDER_FACTOR;
    return voltage;
}

float UpdateBatteryMovingAverage(float new_reading)
{
    // Add new reading to circular buffer
    battery_readings[battery_sample_index] = new_reading;
    battery_sample_index = (battery_sample_index + 1) % BATTERY_SAMPLES;
    
    // Check if we've filled the buffer at least once
    if (!battery_samples_filled && battery_sample_index == 0) {
        battery_samples_filled = true;
    }
    
    // Calculate average
    float sum = 0.0f;
    int count = battery_samples_filled ? BATTERY_SAMPLES : battery_sample_index;
    
    for (int i = 0; i < count; i++) {
        sum += battery_readings[i];
    }
    
    return count > 0 ? sum / count : new_reading;
}

void DisplayBatteryStatus()
{
    // Display battery voltage in top right corner
    tft_display.setCursor(0, 115);
    tft_display.setTextColor(ST7735_TFT::C_WHITE, ST7735_TFT::C_BLACK);
    
    // Choose color based on battery level (using averaged voltage)
    uint16_t voltage_color = ST7735_TFT::C_GREEN;
    if (battery_voltage_average < BATTERY_CRITICAL_THRESHOLD) {
        voltage_color = ST7735_TFT::C_RED;
    } else if (battery_voltage_average < BATTERY_LOW_THRESHOLD) {
        voltage_color = ST7735_TFT::C_YELLOW;
    }
    
    tft_display.setTextColor(voltage_color, ST7735_TFT::C_BLACK);
    char voltage_str[16];
    snprintf(voltage_str, sizeof(voltage_str), "BATTERY: %.2fV", battery_voltage_average);
    tft_display.print(voltage_str);
}

void UpdateAndDisplayBattery()
{
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Update battery voltage measurement with moving average
    if (current_time - last_battery_check >= BATTERY_CHECK_INTERVAL) {
        float raw_voltage = ReadBatteryVoltage();
        battery_voltage_average = UpdateBatteryMovingAverage(raw_voltage);
        battery_voltage = battery_voltage_average; // Keep for compatibility
        last_battery_check = current_time;
    }
    
    // Update battery display
    if (current_time - last_battery_display_update >= BATTERY_DISPLAY_UPDATE_INTERVAL) {
        DisplayBatteryStatus();
        last_battery_display_update = current_time;
    }
}

bool CheckBatteryCritical()
{
    return (battery_voltage_average > 0.0f && battery_voltage_average < BATTERY_CRITICAL_THRESHOLD);
}

void PowerOff()
{
    // Display shutdown message
    tft_display.fillScreen(ST7735_TFT::C_BLACK);
    tft_display.setCursor(30, 60);
    tft_display.setTextColor(ST7735_TFT::C_WHITE, ST7735_TFT::C_BLACK);
    tft_display.print("POWER OFF ...!");
    sleep_ms(1000);
    
    // Turn off power control pin (LOW = PNP blocks = MOSFET blocks = power off)
    gpio_put(SYSTEM_POWER_PIN, 0);
    
    // If we get here, hardware shutdown failed - go to infinite loop
    while(true) {
        sleep_ms(1000);
    }
}

void InitLogicPinsAsInput()
{
    for(int i=0; i<16; i++)  // Initialize all 16 pins at once
    {
        gpio_init(LogicGpioPins[i]);
        gpio_set_dir(LogicGpioPins[i], GPIO_IN);
        gpio_set_pulls(LogicGpioPins[i], false, false);
        //gpio_pull_down(LogicGpioPins[i]);
    }
}

// Fast GPIO bulk read function - reads all GPIO states in one operation
inline uint16_t ReadAllLogicPins()
{
    uint16_t result = 0;
    uint32_t gpio_state = gpio_get_all();
    
    // Extract only the bits we need and pack them into 16-bit result
    for(int i = 0; i < 16; i++) {
        if(gpio_state & (1U << LogicGpioPins[i])) {
            result |= (1U << i);
        }
    }
    return result;
}

// Fast display update - only updates changed logic indicators
inline void UpdateLogicDisplay(uint16_t current_state, uint16_t prev_state)
{
    uint16_t changed_bits = current_state ^ prev_state;
    
    // Only update pins that have changed state
    for(int i = 0; i < 16; i++) {
        if(changed_bits & (1U << i)) {
            int row = (i < 8) ? 0 : 1;
            int col = i % 8;
            int x_pos = 7 + col * 19;
            int y_pos = 37 + row * 50;
            
            uint16_t color = (current_state & (1U << i)) ? ST7735_TFT::C_GREEN : ST7735_TFT::C_BLACK;
            tft_display.fillRect(x_pos, y_pos, 13, 13, color);
        }
    }
}

void DrawDisplayMask()
{
    tft_display.setCursor(0, 0);
    tft_display.print("- Pico Logiktester -");
    
    // Initialize battery voltage with first reading and moving average
    float initial_voltage = ReadBatteryVoltage();
    battery_voltage_average = UpdateBatteryMovingAverage(initial_voltage);
    DisplayBatteryStatus();

    int x_start = 5;
    int y_start = 25;

    tft_display.setTextColor(ST7735_TFT::C_WHITE, ST7735_TFT::C_BLACK);

    for(int i=0; i<8; i++)
    {
        // Draw logic input box 0-7
        tft_display.setCursor(x_start + i * 19 + 4, y_start);
        tft_display.print(i);
        tft_display.drawRectWH(x_start + i * 19, y_start + 10, 17, 17, ST7735_TFT::C_WHITE);
        
        // Draw logic input box 8-15
        tft_display.setCursor(x_start + i * 19 + 4, y_start + 50);
        tft_display.print(i+8, 16);
        tft_display.drawRectWH(x_start + i * 19, y_start + 10 + 50, 17, 17, ST7735_TFT::C_WHITE);
    }
}

int main()
{
    stdio_init_all();

    // onboard LED for debugging
    const uint LED_PIN = 25;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1); // Turn on LED to indicate startup
    
    // Initialize power management first
    InitPowerManagement();
    
    // Initialize battery monitoring
    InitBatteryMonitoring();
    
    // Initialize display with faster SPI speed
    tft_display.TFTInitSPIType(16000, spi0); // Increased from 8000 to 16MHz for faster display updates
    tft_display.setupGPIO(TFT_RST_PIN, TFT_DC_PIN, TFT_CS_PIN, TFT_SCLK_PIN, TFT_SDIN_PIN);
    tft_display.TFTInitPCBType(ST7735_TFT::TFT_ST7735S_Black); // Initialize for BLACK Tab PCB
    tft_display.TFTInitScreenSize(0, 0, TFT_WIDTH, TFT_HEIGHT);
    tft_display.setRotation(displaylib_16_graphics::Degrees_90);
    tft_display.fillScreen(ST7735_TFT::C_BLACK);

    InitLogicPinsAsInput(); // Initialize all logic pins
    DrawDisplayMask();

    uint16_t current_logic_state = ReadAllLogicPins();
    prev_logic_state = current_logic_state;

    // It is important to do an initial display update
    UpdateLogicDisplay(current_logic_state, ~prev_logic_state);

    // Main loop with optimized performance
    while (true)
    {
        // Check for power button long press
        if (CheckPowerButton()) {
            PowerOff(); // This should not return
        }
        
        // Update battery voltage and display periodically (independent of logic changes)
        UpdateAndDisplayBattery();
        
        // Check for critical battery level
        if (CheckBatteryCritical()) {
            tft_display.fillScreen(ST7735_TFT::C_BLACK);
            tft_display.setCursor(40, 60);
            tft_display.setTextColor(ST7735_TFT::C_RED, ST7735_TFT::C_BLACK);
            tft_display.print("LOW BATTERY!");
            sleep_ms(2000);
            PowerOff();
        }
        
        // Fast bulk read of all GPIO pins
        current_logic_state = ReadAllLogicPins();
        
        // Only update logic display if logic state changed
        if(current_logic_state != prev_logic_state) {
            UpdateLogicDisplay(current_logic_state, prev_logic_state);
            prev_logic_state = current_logic_state;
        }
        
        // Reduced sleep for faster response time (5ms instead of 20ms)
        sleep_ms(5);
    }

    return 0;
}
