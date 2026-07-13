import serial
import threading

def read_port(port, baud):
    ser = serial.Serial(port, baud)
    while True:
        line = ser.readline().decode('utf-8').strip()
        print(f"[{port}] {line}")

# Executar em threads separadas
threading.Thread(target=read_port, args=('COM12', 115200)).start()
threading.Thread(target=read_port, args=('COM14', 115200)).start()