Sensor Data Processor with FreeRTOS (STM32F407)

This project is a sensor data processing application designed to run on embedded systems using FreeRTOS. It captures data from multiple sensors, processes it, and then broadcasts the filtered data over Bluetooth Low Energy (BLE). The system consists of two main tasks: producer_task and consumer_task.

Producer Task

The producer task is responsible for reading sensor data at regular intervals and storing it in a circular buffer. It reads data from three different types of sensors: Passive Infrared (PIR) sensor, humidity and heat sensor, and Light Dependent Resistor (LDR). The sensor data is stored in the sensor_buffer array, which is a circular buffer implemented using a mutex to ensure thread-safe access.

Consumer Task

The consumer task waits for the producer task to store data in the buffer. Once data is available, it calculates statistical values (standard deviation, maximum, minimum, and median) for each sensor type. These calculated values are then packaged into a structure called filtered_data_for_ble and broadcasted over BLE using USART.


Usage

To use this project:

Ensure that the necessary hardware peripherals (I2C, UART) are initialized properly in the MX_ functions.

Customize the sensor read functions (i2c_read_sensor_data) according to your sensor setup.

Adjust the buffer size (BUFFER_SIZE) as per your application requirements.

Ensure to update the sensor addresses (PIR_I2C_ADDRESS, HUMIDITY_AND_HEAT_I2C_ADDRESS, LDR_I2C_ADDRESS) in the code to match your sensor configuration.


Dependencies

This project depends on the following libraries:

FreeRTOS
CMSIS
HAL (Hardware Abstraction Layer) for STM32F407 microcontrollers
Note
This README provides a high-level overview of the code structure and functionality. For detailed implementation and configuration, refer to the source files.

Feel free to modify and extend this project according to your specific application needs.
