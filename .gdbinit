set architecture aarch64
file obj/kernel8.elf
target remote localhost:1234

# Uncomment the following line and change
# PATH_TO_PWNDBG to your pwndbg directory to enable pwndbg
source /mnt/e/Github/pwndbg/gdbinit.py
