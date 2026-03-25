savedcmd_/home/xilinx/mldsa_socket/driver/driver.mod := printf '%s\n'   driver.o | awk '!x[$$0]++ { print("/home/xilinx/mldsa_socket/driver/"$$0) }' > /home/xilinx/mldsa_socket/driver/driver.mod
