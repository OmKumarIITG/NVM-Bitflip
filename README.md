# NVM-Bitflip

## Overview

NVM-Bitflip demonstrates running a simple Hello World program using a modified NVMain memory in gem5. This guide covers creating workload files, taking a checkpoint using KVMCPU, and executing the binary workload from the checkpoint.

---

## 1. Create Workload Files

We will prepare binary files to run in Full-System (FS) mode. Since FS mode is very slow, we use precompiled binaries to save time.

### Steps:

1. **Create a workload disk image:**

   * A 2GB workload disk image with the Hello World binary is provided.
   * If we want to create our own, follow this:
          # QEMU Disk Setup Flow
      
      This guide outlines the complete flow for creating and preparing a 2GB disk image to be used in QEMU.
      
      ---
      
      ## Flow Overview
      
      1. **Create a 2GB Raw Disk Image**
      
      ```bash
      qemu-img create -f raw workload.img 2G
      ```
      
      2. **Format the Disk Image**
      
      ```bash
      mkfs.ext4 workload.img
      ```
      
      3. **Boot Using QEMU**
      
      Attach the disk to QEMU and boot the system:
      
      ```bash
      qemu-system-x86_64 \
          -m 2048 \
          -kernel /home/hpca1/.cache/gem5/vmlinux-5.4.0-105-generic \
          -append "root=/dev/sda1 rootfstype=ext4 rw console=ttyS0" \
          -drive file=/home/hpca1/.cache/gem5/x86-ubuntu-18.04-img.img,format=raw,if=ide \
          -drive file=/home/hpca1/BTP/NVM-Bitflip/gem5/gem5_resources/workload.img,format=raw,if=ide \
          -nographic
      ```
      
      4. **Check and Create Partition (if needed)**
      
      Inside the booted system, verify if the disk has a partition:
      
      ```bash
      lsblk
      ```
      
      If `/dev/sdb1` does not exist, create it:
      
      ```bash
      sudo fdisk /dev/sdb
      ```
      
      * Delete partial partitions if any (`d`).
      * Create a new partition (`n → p → 1 → defaults`).
      * Write changes (`w`).
      
      5. **Done**
      
      Verify the partition:
      
      ```bash
      lsblk
      ```
      
      The disk is now ready for use with `/dev/sdb1`.


2. **Boot Ubuntu using QEMU:**

```bash
qemu-system-x86_64 \
    -m 2048 \
    -kernel /home/hpca1/.cache/gem5/vmlinux-5.4.0-105-generic \
    -append "root=/dev/sda1 rootfstype=ext4 rw console=ttyS0" \
    -drive file=/home/hpca1/.cache/gem5/x86-ubuntu-18.04-img.img,format=raw,if=ide \
    -drive file=/home/hpca1/BTP/NVM-Bitflip/gem5/gem5_resources/workload.img,format=raw,if=ide \
    -nographic
```

3. **Check the drive name of the workload image:**

```bash
lsblk
```

4. **Create a mount point:**

```bash
mkdir /mnt/wkld
```

5. **Mount the workload disk image (example sdb1):**

```bash
mount /dev/sdb1 /mnt/wkld
```

6. **Navigate to the mounted disk image:**

```bash
cd /mnt/wkld
```

7. **Create a Hello World program:**

```bash
cat <<EOF > hello_world.cpp
#include <iostream>
using namespace std;

int main() {
    cout << "Hello, World!" << endl;
    return 0;
}
EOF
```

8. **Compile the C++ file:**

```bash
g++ -static hello_world.cpp -o hello_world
```

> **Note:** `-static` ensures that gem5 can run the binary without issues loading dynamic libraries.

9. **Shutdown Ubuntu after creating the workload binary:**

```bash
shutdown now
```

---

## 2. Create a Checkpoint using KVMCPU

To fast-forward to the Region of Interest (ROI), we use KVMCPU.

### Prerequisites:

* Make sure host system supports KVM mode. To this:
  
    (i) Enable Virtualisation in BIOS
  
    (ii) Load KVM modules:
  
  ```bash
      sudo modprobe kvm
      sudo modprobe kvm_intel   # or kvm_amd
  ```

    (iii) Follow offical gem5 guide for next steps : [Setting Up and Using KVM](https://www.gem5.org/documentation/general_docs/using_kvm/)
  
* Create a `checkpoints` directory in the gem5 directory.

### Command to take checkpoint:

```bash
build/X86/gem5.opt configs/deprecated/example/fs.py \
    --kernel=../../../.cache/gem5/x86-linux-kernel-5.4.49 \
    --disk-image=../../../.cache/gem5/x86-ubuntu-18.04-img-5GB.img \
    --disk-image=gem5_resources/workload.img \
    --cpu-type=X86KvmCPU \
    --mem-type=NVMainMemory \
    --mem-size=3GB \
    --caches \
    --l2cache \
    --l1d_size=32kB \
    --l1d_assoc=8 \
    --l1i_size=32kB \
    --l1i_assoc=4 \
    --l2_size=2MB \
    --l2_assoc=16 \
    --checkpoint-dir=checkpoints \
    --script=scripts/1_take_checkpoint.rcS \
    --nvmain-config=/home/hpca1/BTP/NVM-Bitflip/nvmain/Config/RRAM_ISSCC_2012_4GB_NeuroHammer.config \
    --param='system.cpu[0].usePerf=False' \
    --command-line="root=/dev/hda1 console=ttyS0"
```

> **Notes:**
>
> 1. Checkpoints are saved in directories named `cpt.TICKNUMBER`, where `TICKNUMBER` is the tick at which the checkpoint was created.
> 2. Ensure the `checkpoints` folder exists, or checkpoint creation will fail.
> 3. The first disk image is used for Ubuntu files; the second disk image is used for the workload.
> 4. The `nvmain-config` points to the NVMain memory parameters.

---

## 3. Run the Binary Workload Using the Checkpoint

We restore from the checkpoint using `TimingSimpleCPU`.

### Command:

```bash
build/X86/gem5.opt configs/deprecated/example/fs.py \
    --kernel=../../../.cache/gem5/x86-linux-kernel-5.4.49 \
    --disk-image=../../../.cache/gem5/x86-ubuntu-18.04-img-5GB.img \
    --disk-image=gem5_resources/workload.img \
    --restore-with-cpu=TimingSimpleCPU \
    --cpu-type=TimingSimpleCPU \
    --mem-type=NVMainMemory \
    --mem-size=3GB \
    --caches \
    --l2cache \
    --l1d_size=32kB \
    --l1d_assoc=8 \
    --l1i_size=32kB \
    --l1i_assoc=4 \
    --l2_size=2MB \
    --l2_assoc=16 \
    --checkpoint-dir=checkpoints \
    --checkpoint-restore=1 \
    --script=scripts/2_after_boot_script.rcS \
    --nvmain-config=/home/hpca1/BTP/NVM-Bitflip/nvmain/Config/RRAM_ISSCC_2012_4GB_NeuroHammer.config \
    --command-line="root=/dev/hda1 console=ttyS0"
```

> **Important:**
>
> * Follow all three steps in order. Modifying or creating binaries after checkpoint creation will not reflect during execution because gem5 snapshots disk images and memory at checkpoint time.
> * The `--checkpoint-restore` index corresponds to the checkpoint number in the `checkpoints` directory, sorted by tick.
> * Ensure `nvmain-config` and all script paths are correct.

## Flow Diagram: 
![svgviewer-output (1)](https://github.com/user-attachments/assets/747e633e-f26b-4218-895a-d991dc051853)

## References
1. Hammulator : https://github.com/cispa/hammulator
