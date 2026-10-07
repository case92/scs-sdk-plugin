# TruckSim GPS Telemetry Plugin

Native DLL plugin for Euro Truck Simulator 2 and American Truck Simulator that streams real-time telemetry data via shared memory. Used by the [PC Telemetry Server](https://github.com/TruckSim-GPS/trucksim-gps-server) to provide truck position and game state to the TruckSim GPS mobile app.

Fork of [RenCloud/scs-sdk-plugin](https://github.com/RenCloud/scs-sdk-plugin), originally based on [nlhans/ets2-sdk-plugin](https://github.com/nlhans/ets2-sdk-plugin).

## How It Works

The plugin registers with the SCS Telemetry SDK and writes game data into a Memory Mapped File (MMF) at `Local\TSGPSTelemetry` (32 KB). The PC Telemetry Server reads this shared memory region to serve telemetry over HTTP.

The data structure is defined in `scs-telemetry/inc/scs-telemetry-common.hpp` (`scsTelemetryMap_t`). The C# reader that parses this struct has been moved to the [PC Telemetry Server](https://github.com/TruckSim-GPS/trucksim-gps-server) repository under `source/Funbit.Ets.Telemetry.Server/SCSSdkClient/`.

## Building

Requires Visual Studio 2022 with the v143 C++ toolset.

**Solution:** `scs-telemetry/vs2012/scs-telemetry.sln`

Build both `Release|x64` and `Release|Win32` configurations. Output DLLs are named `trucksim-gps-telemetry.dll`.

After building, copy the DLLs into the server's bundled plugin directory and update the MD5 hashes in `PluginSetup.cs`. See the [trucksim-gps-server](https://github.com/TruckSim-GPS/trucksim-gps-server) README for the full distribution workflow.

## Installation

The PC Telemetry Server handles DLL installation automatically. It copies the bundled plugin to `{GamePath}/bin/win_x64/plugins/` and `{GamePath}/bin/win_x86/plugins/` during its setup wizard.

For manual installation, place the DLL in the game's `bin/win_x64/plugins/` directory (create the `plugins` folder if it doesn't exist).
