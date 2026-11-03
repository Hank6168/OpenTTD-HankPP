# OpenTTD-HankPP

**English** | [简体中文](README_zh-CN.md)

![Version](https://img.shields.io/badge/version-v0.9--beta2-blue)
![Base](https://img.shields.io/badge/base-OpenTTD%20JGRPP%200.73.1-green)
![License](https://img.shields.io/badge/license-GPL--2.0-orange)

> **Current public release:** v0.9-beta2  
>
> **Feature status:**
> - **[v0.9]** — Available in the current v0.9 public release series.
> - **[v1.0-ready]** — Core development is complete; release is pending the remaining v1.0 work and release validation.
> - **[v1.0-dev]** — In development for v1.0; not necessarily available in public builds.
> - **[v1.0+]** — A long-term feature area to be expanded from v1.0 onward.
> - **[v0.9 → v1.0-ready]** — Builds on existing v0.9 functionality, with v1.0 enhancements substantially complete and awaiting release.

## Introduction

This project is a China-oriented extension based on **OpenTTD-JGRPP**.

It focuses on Chinese-style transport and railway construction gameplay. While retaining most of JGRPP's gameplay features,
it further expands upon:

- Urban development simulation
- Land value system
- Actual building population
- Residential occupancy rates
- Intercity economic connections and passenger flows
- Railway operation mechanisms
- Chinese-style train operations and trainset rotations

The goal is to build upon JGRPP
and gradually create a game that more closely reflects **Chinese railways and localized gameplay**.

If you encounter any issues, feel free to report them through GitHub Issues or the project development group:

HankPP Development Group: 1074303090

# Main Features

## Cities and Land Value [v0.9]

HankPP introduces an independent urban land value system,
laying the foundation for future extensions to urban development and related economic mechanisms.

Available in the current release:

- Urban land value scoring
- Land value based on multiple factors
- Monthly land value updates
- Economic effects driven by land value

In the future, this system will also interact with urban development, population, buildings, transport conditions, and other mechanisms.


## Buildings and Population [v1.0-ready]

HankPP expands the capacity of vanilla buildings and certain GRF buildings, establishing a more realistic relationship between building size and residential population.

Implemented features (awaiting the v1.0 release):

- Actual building population calculation
- Residential occupancy rates
- Actual city population statistics
- Identification of residential, commercial, industrial, and other building types


##  Urban Development Optimization [v0.9 → v1.0-ready]

The original city growth mechanics are extended to account for development demand, residential density, regional development, and other factors.

Existing features and v1.0-ready enhancements:

- Urban development demand calculation
- Residential density adjustment
- Regional development effects
- Urban growth simulation

##  Economy and Passenger Demand [v1.0-ready]

HankPP aims to model travel demand between relatively distant, developed regions.

The model primarily considers:

- City population
- City development level
- Distance between cities
- Intercity corridors
- Intercity demand

The aim is for cities with larger populations, higher development levels, and reasonable distances between them
to form more prominent transport links and economic corridors, similar to the Beijing–Shanghai corridor.


##  Data and Statistics [v1.0-dev]

Feature scope under development (some foundations are already implemented):

- Train operations data
- Line opening and operating history
- Passenger flow observations and transport feedback
- Intercity demand statistics


##  Catenary and Infrastructure [v1.0-ready]

Implemented features (awaiting the v1.0 release):

- Chinese-style masts, portal structures, and contact wires
- Automated catenary placement and intelligent grouping
- Single-track, double-track, and multi-track portal systems
- Dedicated layouts for stations, turnouts, station throats, and terminal areas
- Automated placement and fine-grained manual adjustment
- Catenary style variations for different railway types and construction periods


##  Train Services and Trainset Rotations [v1.0-dev]

Feature scope under development (some foundations are already implemented):

- Rotation and train service identities independent of physical trains
- Temporary trainset substitution, hot-spare trainsets, and breakdown replacements
- Reserve trainsets and candidate vehicles
- Rotation conflict and train status evaluation
- Integration of departure information with trainset plans
- Rotation synchronization, save/load, and recovery in multiplayer games

## Chinese Localization Extensions [v1.0+]

Chinese-style content is gradually being added to the game.

Continuing expansion includes:

- A fully localized Chinese interface and improved on-screen information
- Chinese-style station information boards


# 💻 Development Platforms
| Platform | Current Status |
| --- | --- |
| Windows x64 | ✅ Latest Version |
| Windows x86 | Coming Soon |
| Linux | ✅ Latest Version |
| macOS | Coming Soon |
| ARM | Coming Soon |
| Android | Coming Soon |


# 📜 License

OpenTTD-HankPP is developed based on OpenTTD-JGRPP and complies with its open-source licensing requirements.

This project is licensed under:

**GNU General Public License v2.0 (GPL-2.0)**

For the full license text, please refer to the `COPYING.md` / `LICENSE` files in the project.
