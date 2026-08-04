# OpenTTD-HankPP

![Version](https://img.shields.io/badge/version-v0.9--beta1-blue)
![Base](https://img.shields.io/badge/base-OpenTTD%20JGRPP%200.73.0-green)
![License](https://img.shields.io/badge/license-GPL--2.0-orange)

## 中文简介

OpenTTD-HankPP 是基于 **OpenTTD JGRPP 0.73.0** 开发的增强版本。

本项目致力于在保持 OpenTTD 核心运输经营玩法的基础上，引入更加深入的：

- 城市发展模拟
- 土地价值系统
- 区域经济模型
- 铁路运营扩展

为未来构建更加真实的城市—交通—经济综合模拟体系提供基础框架。


## English Introduction

OpenTTD-HankPP is an enhanced development branch based on **OpenTTD JGRPP 0.73.0**.

The project aims to extend the original OpenTTD gameplay with advanced simulation systems, including:

- Urban development simulation
- Land value system
- Regional economic modeling
- Railway operation extensions

The goal is to build a more realistic city-transportation-economic simulation environment.


# Features / 主要功能

## 🏙 Land Value System / 土地价值系统

Implemented:

- Town land value score system
- Distance-based land value calculation
- Land value cache mechanism
- Monthly land value update loop
- Land value related economic effects


## 🏘 Urban Development Model / 城市发展模型

Implemented:

- Town development demand calculation
- Housing density adjustment
- Regional development influence
- Urban growth simulation framework


## 🌐 Intercity Economic Gravity / 城市经济联系

Implemented:

- Economic interaction between towns
- Population influence
- Development level influence
- Distance attenuation model

# Compatibility / 兼容性

## Windows

Currently tested:

✅ Windows x64


Build environment:

OS:
Windows 10/11 x64

Compiler:
MSVC 19.51

Build System:
CMake

Language:
C++20




## Other Platforms

Other platforms have not been fully tested.

Currently not guaranteed:

- Linux
- macOS
- ARM platforms


Although OpenTTD/JGRPP supports multiple platforms, HankPP development and testing are currently focused on Windows x64.

Successful compilation and operation on other platforms cannot be guaranteed at this stage.

Community testing and feedback are welcome.


# Save Compatibility / 存档兼容性

Due to internal data structure changes:

- Vanilla OpenTTD saves are not guaranteed to be compatible.
- Unmodified JGRPP saves are not guaranteed to be compatible.

Please backup your save files before upgrading.


# Roadmap / 开发计划

## Future Development

Planned features:

- Advanced urban simulation
- Railway operation system
- Chinese railway related features
- Station information display system
- Transportation-oriented development (TOD)


# License / 许可证

OpenTTD-HankPP is released under the GNU General Public License v2.0 (GPL-2.0).

The project follows the licensing model of OpenTTD/JGRPP.
