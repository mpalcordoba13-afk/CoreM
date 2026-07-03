CC      = i686-elf-gcc
LD      = i686-elf-ld
NASM    = nasm

CFLAGS  = -std=c99 -ffreestanding -O2 -Wall -Wextra \
           -Iinclude -nostdlib \
           -fno-builtin -fno-stack-protector \
           -m32

LDFLAGS = -T linker.ld -m elf_i386 --oformat elf32-i386

ASM_SRCS = boot/boot.asm kernel/gdt_flush.asm kernel/idt_asm.asm

C_SRCS   = kernel/kernel.c kernel/gdt.c kernel/idt.c \
           drivers/vga.c drivers/keyboard.c drivers/mouse.c \
           drivers/framebuffer.c drivers/bochsvbe.c \
           drivers/rtc.c drivers/sound.c drivers/timer.c \
           drivers/cpuinfo.c drivers/meminfo.c drivers/pci.c drivers/battery.c \
           drivers/usb_uhci.c drivers/usb_printer.c drivers/usb_msd.c drivers/fat32.c drivers/usb_ehci.c \
           drivers/rtl8139.c drivers/rtl8169.c drivers/ndis.c \
           drivers/net.c drivers/tcp.c drivers/dhcp.c drivers/tls.c \
           gui/gui.c gui/wallpaper.c gui/calc.c \
           gui/fs.c gui/users.c gui/bootscreen.c gui/login.c \
           gui/terminal.c gui/filemanager.c gui/notepad.c gui/sysmonitor.c \
           gui/settings.c gui/snake.c gui/tetris.c gui/pacman.c gui/pong.c gui/browser.c gui/trash.c \
           gui/volume.c gui/tips.c gui/gallery.c gui/scenes.c \
           gui/assets.c gui/imageviewer.c gui/musicplayer.c gui/guide.c \
           gui/pciviewer.c gui/usbexplorer.c gui/desktop.c gui/code.c \
           gui/toast.c gui/syslog.c gui/lockscreen.c gui/screensaver.c \
           gui/paint.c gui/spreadsheet.c gui/calendar.c gui/alarm.c \
           gui/minesweeper.c gui/game2048.c gui/chess.c gui/breakout.c \
           gui/conway.c

ASM_OBJS = $(ASM_SRCS:.asm=.o)
C_OBJS   = $(C_SRCS:.c=.o)
ALL_OBJS = $(ASM_OBJS) $(C_OBJS)
KERNEL   = myos.kernel

.PHONY: all clean run run-usb run-net run-all debug

all: $(KERNEL)

%.o: %.asm
	$(NASM) -f elf32 $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(ALL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo ">>> Kernel compilado: $(KERNEL)"

run: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std \
	    -usb -device usb-tablet

run-usb: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std \
	    -usb -device usb-tablet \
	    -device usb-ehci,id=ehci \
	    -drive if=none,id=usbstick,file=usbdisk.img,format=raw \
	    -device usb-storage,bus=ehci.0,drive=usbstick

run-net: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std \
	    -usb -device usb-tablet \
	    -netdev user,id=net0 -device rtl8139,netdev=net0

run-all: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std \
	    -usb -device usb-tablet \
	    -device usb-ehci,id=ehci \
	    -drive if=none,id=usbstick,file=usbdisk.img,format=raw \
	    -device usb-storage,bus=ehci.0,drive=usbstick \
	    -netdev user,id=net0 -device rtl8139,netdev=net0

debug: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 64M -vga std \
	    -usb -device usb-tablet -serial stdio -no-reboot

clean:
	rm -f $(ALL_OBJS) $(KERNEL)
	@echo "Limpiado."

# ---- Booteable en hardware real ----
# Requiere: grub-pc-bin, grub-common, xorriso  (sudo apt install grub-pc-bin xorriso)
iso: $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/myos.kernel
	printf 'set default=0\nset timeout=5\nmenuentry "MyOS" {\n    multiboot /boot/myos.kernel\n    boot\n}\n' > iso/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso iso/ 2>&1 | tail -3
	@echo ">>> ISO lista: myos.iso"
	@echo ">>> Grabar en USB: dd if=myos.iso of=/dev/sdX bs=4M status=progress"

usb: myos.iso
	@echo "Uso: sudo dd if=myos.iso of=/dev/sdX bs=4M status=progress && sync"
	@echo "(reemplaza /dev/sdX con tu pendrive, ej: /dev/sdb)"
