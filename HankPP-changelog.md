# HankPP Changelog

# HankPP 0.9-beta1

Release status:

Beta Release

Based on:

JGRPP 0.73.0


---

# Overview

HankPP 0.9-beta1 is the first public beta release of HankPP.

This version introduces a complete land value and urban economic simulation framework based on JGRPP.

The goal is to improve OpenTTD's city development simulation by introducing:

- Dynamic land value
- Urban development demand
- Infrastructure economic influence
- Intercity economic interaction


---

# Features


## Patch 001 - Land Value Core

Added:

- Town land value score system
- Fixed-point land value calculation
- Distance attenuation model
- Land value query interface


## Patch 002 - Town Persistence

Added:

- Persistent town land value score
- Save/load support
- Legacy save initialization


## Patch 003 - Land Value Cache

Added:

- Town land value cache
- Monthly land value update
- Stable ranking system


## Patch 004 - Settings and GUI

Added:

- Land value settings
- Land value display
- Debug command support


## Patch 005 - Land Purchase Cost

Added:

- Land value influence on land purchase
- Additional land acquisition cost


## Patch 006 - Infrastructure Cost

Added:

- Land value influence on infrastructure construction
- Railway, road and station economic influence


## Patch 007 - Town Development Demand

Added:

- Residential demand evaluation
- Commercial/industrial demand framework
- Town development indicators


## Patch 008 - Land Use and House Density

Added:

- Land value influence on house density selection
- Urban land-use adjustment


## Patch 009 - Intercity Economic Gravity

Added:

- Economic relationship calculation between major towns
- Intercity demand evaluation
- Economic gravity ranking


---

# Compatibility

HankPP is based on JGRPP 0.73.0.

Save games created by HankPP should be loaded with HankPP.


Compatibility with vanilla OpenTTD is not guaranteed.


---

# Known Limitations

The following systems are not yet modified:

- Actual passenger generation
- CargoDist economic flow
- Industry relocation
- Airport and port economic influence
- Advanced regional planning


---

# Future Development

Possible future versions may include:

- Better passenger demand simulation
- Transport corridor effects
- Regional economic networks
- More detailed city development models


---

# Credits

See:

HankPP-CREDITS.md