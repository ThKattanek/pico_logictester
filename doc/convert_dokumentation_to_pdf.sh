#!/bin/bash
sed 's/├/+/g; s/└/+/g; s/┘/+/g; s/┐/+/g; s/┌/+/g; s/─/-/g; s/│/|/g; s/┤/+/g; s/←/<-/g; s/→/->/g; s/°/deg/g; s/Ω/Ohm/g; s/µ/u/g' pico_logictester_dokumentation.md | pandoc -o pico_logictester_dokumentation.pdf
