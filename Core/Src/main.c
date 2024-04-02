#include <stdbool.h>
#include "main.h"
#include "cmsis_os.h"
#include <time.h>
#include <math.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/
// Define the structure to hold sensor data
typedef struct {
    float PIR;
    float humidity_and_heat;
    float LDR;
} sensor_data_t;

// Define the enum for different sensor types
typedef enum {
    SENSOR_PIR,
    SENSOR_HUMIDITY_AND_HEAT,
    SENSOR_LDR
} sensor_t;

// Define the structure to hold filtered data for BLE
typedef struct {
    float pir_std_dev;
    float pir_max;
    float pir_min;
    float pir_median;
    float humidity_and_heat_std_dev;
    float humidity_and_heat_max;
    float humidity_and_heat_min;
    float humidity_and_heat_median;
    float ldr_std_dev;
    float ldr_max;
    float ldr_min;
    float ldr_median;
} filtered_data_for_ble;

// Task function prototypes
void producer_task(void *argument);
void consumer_task(void *argument);

// Define buffer size and sensor I2C addresses
#define BUFFER_SIZE 100
#define PIR_I2C_ADDRESS 0x01
#define HUMIDITY_AND_HEAT_I2C_ADDRESS 0x02
#define LDR_I2C_ADDRESS 0x03
//#define BLE_USART_ADDRESS 0x04

/* Private variables ---------------------------------------------------------*/
sensor_data_t sensor_buffer[BUFFER_SIZE];
int buffer_head = 0, buffer_tail = 0;

// Hardware peripherals
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

// FreeRTOS handles
// Create threads, mutex and semaphore
osThreadId producer_task_handle;
osThreadId consumer_task_handle;

osMutexId sensor_buffer_mutex;
osMutexDef(sensor_buffer_mutex);

osSemaphoreId producer_semaphore;
osSemaphoreDef(producer_semaphore);

osSemaphoreId consumer_semaphore;
osSemaphoreDef(consumer_semaphore);

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);

// Function prototypes for sensor operations
float i2c_read_sensor_data(uint8_t device_address, sensor_t sensor_type);
bool buffer_push(sensor_data_t data);
void broadcast_ble(filtered_data_for_ble filtered_data);

// Function prototypes for data processing
float calculate_std_dev(float data[], uint32_t count);
float calculate_max(float data[], uint32_t count);
float calculate_min(float data[], uint32_t count);
float calculate_median(float data[], uint32_t count);

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    // System initialization for USART/UART, I2C communication and data processing
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();

    // Initialize FreeRTOS resources
    sensor_buffer_mutex = xSemaphoreCreateMutex();
    producer_semaphore = xSemaphoreCreateBinary();
    consumer_semaphore = xSemaphoreCreateBinary();

    // Create producer and consumer tasks
    xTaskCreate(producer_task, "ProducerTask", configMINIMAL_STACK_SIZE, NULL, 1, &producer_task_handle);
    xTaskCreate(consumer_task, "ConsumerTask", configMINIMAL_STACK_SIZE, NULL, 1, &consumer_task_handle);

    // Start FreeRTOS scheduler
    vTaskStartScheduler();

    // The program should never reach here
    while (1) {}
}


void producer_task(void *argument) {
    while (1) {
        // Wait for producer semaphore
        xSemaphoreTake(producer_semaphore, portMAX_DELAY);

        // Access shared sensor buffer with mutex
        osMutexWait(sensor_buffer_mutex, osWaitForever);
        // Read sensor data and push it into the buffer
        for(int i = 0; i < 30; ++i) {
            osDelay(1000);  // Delay for 1 second

            sensor_data_t sensor_data;
            sensor_data.PIR = i2c_read_sensor_data(PIR_I2C_ADDRESS, SENSOR_PIR);
            sensor_data.humidity_and_heat = i2c_read_sensor_data(HUMIDITY_AND_HEAT_I2C_ADDRESS, SENSOR_HUMIDITY_AND_HEAT);
            sensor_data.LDR = i2c_read_sensor_data(LDR_I2C_ADDRESS, SENSOR_LDR);

            if (!buffer_push(sensor_data)) {
                // If buffer is full, discard oldest data
                buffer_head = (buffer_head + 1) % BUFFER_SIZE;
                // And push the newest data again
                buffer_push(sensor_data);
            }
        }
        // Release Mutex
        osMutexRelease(sensor_buffer_mutex);

        // Signal consumer task
        xSemaphoreGive(consumer_semaphore);
    }
}


void consumer_task(void *argument) {
    /* Consumer thread waits the condition variable which indicates
     * that there are at least one entry in the circular thread queue.
     * If the variable is set then takes mutex, read data,
     * release mutex and set condition variable which indicates
     * that there are at least one entry in the circular queue.
     * After this it can apply and filter to the data.
     */
    while (1) {
        // Wait for consumer semaphore
        xSemaphoreTake(consumer_semaphore, portMAX_DELAY);

        // Access shared sensor buffer with mutex
        osMutexWait(sensor_buffer_mutex, osWaitForever);

        // Read all sensor data from the buffer and write it to temp data array
        float pir_data[BUFFER_SIZE];
        float humidity_and_heat_data[BUFFER_SIZE];
        float ldr_data[BUFFER_SIZE];

        for (int i = 0; i < BUFFER_SIZE; ++i) {
            pir_data[i] = sensor_buffer[i].PIR;
            humidity_and_heat_data[i] = sensor_buffer[i].humidity_and_heat;
            ldr_data[i] = sensor_buffer[i].LDR;
        }
        // Release Mutex
        osMutexRelease(sensor_buffer_mutex);
        // And Semaphore
        xSemaphoreGive(producer_semaphore);

        // Calculate statistics for each sensor data type
        float pir_std_dev = calculate_std_dev(pir_data, BUFFER_SIZE);
        float pir_max = calculate_max(pir_data, BUFFER_SIZE);
        float pir_min = calculate_min(pir_data, BUFFER_SIZE);
        float pir_median = calculate_median(pir_data, BUFFER_SIZE);

        float humidity_and_heat_std_dev = calculate_std_dev(humidity_and_heat_data, BUFFER_SIZE);
        float humidity_and_heat_max = calculate_max(humidity_and_heat_data, BUFFER_SIZE);
        float humidity_and_heat_min = calculate_min(humidity_and_heat_data, BUFFER_SIZE);
        float humidity_and_heat_median = calculate_median(humidity_and_heat_data, BUFFER_SIZE);

        float ldr_std_dev = calculate_std_dev(ldr_data, BUFFER_SIZE);
        float ldr_max = calculate_max(ldr_data, BUFFER_SIZE);
        float ldr_min = calculate_min(ldr_data, BUFFER_SIZE);
        float ldr_median = calculate_median(ldr_data, BUFFER_SIZE);

        // Pack the filtered data for BLE transmission
        filtered_data_for_ble filtered_data;
        filtered_data.pir_std_dev = pir_std_dev;
        filtered_data.pir_max = pir_max;
        filtered_data.pir_min = pir_min;
        filtered_data.pir_median = pir_median;
        filtered_data.humidity_and_heat_std_dev = humidity_and_heat_std_dev;
        filtered_data.humidity_and_heat_max = humidity_and_heat_max;
        filtered_data.humidity_and_heat_min = humidity_and_heat_min;
        filtered_data.humidity_and_heat_median = humidity_and_heat_median;
        filtered_data.ldr_std_dev = ldr_std_dev;
        filtered_data.ldr_max = ldr_max;
        filtered_data.ldr_min = ldr_min;
        filtered_data.ldr_median = ldr_median;

        // Broadcast filtered data over BLE
        broadcast_ble(filtered_data);
    }
}

// Function to push data into the buffer
bool buffer_push(sensor_data_t data) {
    int next_head = (buffer_head + 1) % BUFFER_SIZE;
    if (next_head != buffer_tail) {
        sensor_buffer[buffer_head] = data;
        buffer_head = next_head;
        return true;
    }
    return false;
}


// Function to read sensor data from I2C
float i2c_read_sensor_data(uint8_t device_address, sensor_t sensor_type) {
    uint8_t data[2]; // Buffer for received data
    HAL_I2C_Master_Receive(&hi2c1, device_address << 1, data, sizeof(data), HAL_MAX_DELAY); // Read data from I2C device
    // Process and convert received data appropriately
    float sensor_data = (float)((data[0] << 8) | data[1]); // Example: Convert 16-bit data to float
    return sensor_data;
}

// Function to transmit data over BLE
void broadcast_ble(filtered_data_for_ble filtered_data) {
    // Package data for transmission over USART to BLE device
    uint8_t data[48]; // 4 bytes x 12 data
    memcpy(data, &filtered_data, sizeof(filtered_data_for_ble));
    // Transmit data over USART to BLE
    HAL_UART_Transmit(&huart2, data, sizeof(data), HAL_MAX_DELAY);
}

// Function to calculate standard deviation
float calculate_std_dev(float data[], uint32_t count) {
    float sum = 0.0, mean, std_dev = 0.0;

    // Calculate sum
    for(uint32_t i = 0; i < count; ++i) {
        sum += data[i];
    }

    // Calculate mean
    mean = sum / count;

    // Calculate standard deviation
    for(uint32_t i = 0; i < count; ++i) {
        std_dev += pow(data[i] - mean, 2);
    }

    return sqrt(std_dev / count);
}

// Function to find maximum value
float calculate_max(float data[], uint32_t count) {
    float max = data[0];
    for(uint32_t i = 1; i < count; ++i) {
        if(data[i] > max) {
            max = data[i];
        }
    }
    return max;
}

// Function to find minimum value
float calculate_min(float data[], uint32_t count) {
    float min = data[0];
    for(uint32_t i = 1; i < count; ++i) {
        if(data[i] < min) {
            min = data[i];
        }
    }
    return min;
}

// Function to find median value
float calculate_median(float data[], uint32_t count) {
    float temp;
    // Sort the data in ascending order
    for(uint32_t i = 0; i < count - 1; ++i) {
        for(uint32_t j = i + 1; j < count; ++j) {
            if(data[i] > data[j]) {
                temp = data[i];
                data[i] = data[j];
                data[j] = temp;
            }
        }
    }
    // If odd number of elements, return the middle value
    if(count % 2 != 0) {
        return data[count / 2];
    }
    // If even number of elements, return the average of the two middle values
    return (data[(count - 1) / 2] + data[count / 2]) / 2.0;
}

// System clock configuration
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 50;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }
}

// I2C1 initialization
static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

// USART2 initialization
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

// GPIO initialization
static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
}

// Error handler function
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

// Assertion failed callback
#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
