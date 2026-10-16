# M5Unit-ENV

This driver adapts the QMP6988 compensation and OTP calibration logic, the
QMP6988 calibration layout, and SHT30 identification and CRC handling from
M5Stack M5Unit-ENV at commit
737e5180ea7b84d8f22fe49ae4fc62cf06cbc3a2.

Source files:

- src/unit/unit_QMP6988.cpp, compensation functions and calibration decode
- src/unit/unit_QMP6988.hpp, qmp6988::Calibration layout
- src/unit/unit_SHT30.cpp, serial-number identification and measurement CRC

Source: https://github.com/m5stack/M5Unit-ENV/tree/737e5180ea7b84d8f22fe49ae4fc62cf06cbc3a2

Copyright (c) 2021 M5Stack
Copyright (c) 2024 M5Stack Technology CO LTD

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
