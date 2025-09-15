#######################################
# QEMU targets
#######################################

# QEMU targets
build-qemu: build-frontend build-idf-project-qemu

build-idf-project-qemu:
	@echo "Building for QEMU with OpenEth ethernet driver"
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal $(addprefix -D, $(DEFS)) build

qemu-flash-image: build-qemu
	@echo "Generating QEMU flash image..."
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal qemu || \
	(cd build && python -m esptool --chip=esp32 merge_bin --output=qemu_flash.bin \
	 --fill-flash-size=4MB --flash_mode dio --flash_freq 40m --flash_size 4MB \
	 0x1000 bootloader/bootloader.bin 0x10000 mge.bin 0x8000 partition_table/partition-table.bin)

qemu-monitor: build-qemu
	@echo "Starting QEMU monitor..."
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal monitor

qemu-run: qemu-flash-image
	@echo "Running in QEMU..."
	@echo "Note: Access web interface at http://localhost:8080"
	@echo "Use ./run_qemu_with_web.sh for web access with port forwarding"
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal qemu monitor

qemu-web: qemu-flash-image
	@echo "Running QEMU with web server port forwarding..."
	@echo "Web interface will be available at: http://localhost:8080"
	./run_qemu_with_web.sh

# Help target for QEMU
qemu-help:
	@echo "QEMU Targets:"
	@echo "  build-qemu      - Build project for QEMU emulation"
	@echo "  qemu-flash-image- Generate QEMU flash image"
	@echo "  qemu-run        - Run in QEMU (basic mode)"
	@echo "  qemu-web        - Run in QEMU with web access (localhost:8080)"
	@echo "  qemu-monitor    - Run QEMU with monitor only"
	@echo ""
	@echo "Quick start: make qemu-web"

clean-qemu:
	idf.py -DSDKCONFIG=sdkconfig.qemu.minimal fullclean
	rm -rf build

.PHONY: build-qemu build-idf-project-qemu qemu-flash-image qemu-monitor qemu-run qemu-web qemu-help clean-qemu
