# 🌌 CHAOS-SYMBIOSE OS — Autonomous Agent Journal (Jules.md)

## Data Archiving Phase 1: OS Boot & Filesystem Architecture

### Chaos 1.5 Ramdisk Extraction (`CHAOS.RDZ`)
- The original `CHAOS.RDZ` is a compressed **Minix V1 Filesystem** with a magic value of `0x138f` and 4096 inodes.
- Using `guestfish` from `libguestfs-tools` (with backend direct to avoid QEMU container issues), we were able to mount `/dev/sda` via a loop device onto the appliance and extract its contents into a standard `tar.gz` format.
- The root filesystem structure has been extracted into `06_OpenMosix_Exploration` for deep investigation.

### Key Directories and Configs
- `/etc/openmosix.map` - Contains static mapping IP addresses to OpenMosix node-numbers. Mentions the autodiscovery daemon assigns node-numbers automatically to all visible OpenMosix machines.
- Various `init` scripts are used to bootstrap OpenMosix services like `mosctl` and `omdiscd`.

## Data Archiving Phase 2: OpenMosix Core Mechanics & System APIs

### OpenMosix Binaries
The following OpenMosix binaries are present in `/sbin/`:
- `mosctl` - OpenMosix control utility (ELF 32-bit executable).
- `moslimit` - Manages/enforces limits on Mosix processes.
- `mosmon` - OpenMosix monitor (typically uses ncurses).
- `mosrun` - Runs a program with specific OpenMosix attributes.
- `omdiscd` - OpenMosix autodiscovery daemon.

All are standard 32-bit ELF binaries dynamically linked with `libc.so.6`.

## Data Archiving Phase 3: Cluster Topology & Networking Protocol

### Node Discovery (`omdiscd`)
- The `omdiscd` daemon handles the dynamic cluster topology. It uses UDP broadcast/multicast to discover active OpenMosix nodes and update the cluster mapping dynamically if `/etc/openmosix.map` is empty or only partially populated.


### The PID 1 (`/sbin/init`)
- `init` in Chaos 1.5 is a statically-linked/dynamic ELF binary rather than a standard script or SysVinit structure.
- Upon examining its strings, we see it invokes commands like:
  - `/sbin/acpid`
  - `/sbin/ntpd -g`
  - `/sbin/omdiscd` (Autodiscovery daemon for OpenMosix)
  - `setpe -off` (presumably controls openMosix process execution)
  - It also interacts heavily with `/proc/hpc/admin/` files (which was the old sysfs/procfs path for OpenMosix configuration).


### Procfs Interface (`/proc/hpc/`)
- OpenMosix relied extensively on a `procfs` interface at `/proc/hpc/` (historically Mosix used `/proc/mosix/` but Chaos/OpenMosix standardized around `hpc`).
- Strings from binaries show interaction with:
  - `/proc/hpc/admin/block` (Prevent migration)
  - `/proc/hpc/admin/bring` (Bring home migrated processes)
  - `/proc/hpc/admin/expel` (Expel guest processes)
  - `/proc/hpc/admin/lstay` / `stay` (Lock processes to local node)
- The cluster map is often passed directly via these `procfs` nodes (or `/proc/hpc/admin/config`).

### Control Commands (`setpe`, `mosctl`)
- `setpe` is heavily referenced. `setpe -off` disables the Mosix extensions on the node, while `setpe -on` (or similar) enables them. It interacts with the kernel to register the local node ID and IP address into the Mosix table.
- `mosctl` handles more granular configurations such as setting tuning parameters, managing the LSA (Local Socket Access) configurations, and defining process migration behaviors.


### System APIs and Tuning Overheads
The OpenMosix `mosctl` utility manages the kernel's process migration heuristics via proc files such as:
- `/proc/hpc/admin/overheads`
- `/proc/hpc/admin/decayinterval`, `slowdecay`, `fastdecay`
- Limits and load balancers: `/proc/hpc/admin/loadlimit`, `cpulimit`
It also exposes process-specific configuration:
- `/proc/self/lock`
- `/proc/self/migrate`
- `/proc/self/where` (shows current node execution)
- `/proc/self/nmigs` (number of migrations)


## Data Archiving Phase 3: Cluster Topology & Networking Protocol

### Node Communication Protocol
- Nodes use standard TCP/UDP networking (over IPv4).
- `omdiscd` (OpenMosix discovery daemon) utilizes UDP multicast to auto-discover peers without requiring a centralized configuration server.
- The `mosmon` binary gathers cluster state metrics by polling the local `/proc/hpc/nodes/` directory.

### Integration with HiveMind
- The Chaos 1.5 baseline networking and discovery are based entirely on kernel-level IP clustering.
- In our target Blueprint (`03_HiveMind_Orchestrator`), this legacy UDP/TCP structure will be replaced or supplemented by the custom **IRCv3 Neural Bus** (`symbiose_ircd.exe`). The IRC channel `#cluster-announce` will act as the new auto-discovery mechanism, routing JSON payloads over shared memory for sub-microsecond latency, bypassing the traditional Linux networking stack when operating in the WHPX container.
