#include "sense.h"
#include "device.h"
#include "log.h"

#include "stm32l4xx_ll_gpio.h"
#include "stm32l4xx_hal_tsc.h"

/**
USB A Nano TSC GPIO Configuration:
PB4   ------> Channel 1 (electrode 1)
PB5   ------> Channel 2 (electrode 2)
PB6   ------> Channel 3 (sampling capacitor)
PB7   ------> Channel 4 (unused)

USB C Touch TSC GPIO Configuration
PB4   ------> Channel 1 (sampling cap)
PB5   ------> Channel 2 (electrode 2)
PB6   ------> Channel 3 (unused)
PB7   ------> Channel 4 (electrode 1)
*/
int get_sampling_cap_io(void)
{
    return nfc_peripheral_exists() ? TSC_GROUP2_IO1 : TSC_GROUP2_IO3;
}

int get_sampling_cap_pin(void)
{
    return nfc_peripheral_exists() ? LL_GPIO_PIN_4 : LL_GPIO_PIN_6;
}

int get_first_electrode_io(void)
{
    return nfc_peripheral_exists() ? TSC_GROUP2_IO4 : TSC_GROUP2_IO1;
}

int get_first_electrode_pin(void)
{
    return nfc_peripheral_exists() ? LL_GPIO_PIN_7 : LL_GPIO_PIN_4;
}

int get_second_electrode_io(void)
{
    return TSC_GROUP2_IO2;
}

int get_second_electrode_pin(void)
{
    return LL_GPIO_PIN_5;
}

int get_tsc_threshold(void)
{
    // Threshold for USB A nano is 45
    // Threshold for USB C touch is not yet calibrated so this is a dummy value
    return nfc_peripheral_exists() ? 59 : 45;
}

void tsc_init(void)
{
    LL_GPIO_InitTypeDef GPIO_InitStruct;
    // Enable TSC clock
    RCC->AHB1ENR |= (1<<16);

    GPIO_InitStruct.Pin = get_first_electrode_pin() | get_second_electrode_pin();
    GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Alternate = LL_GPIO_AF_9;
    LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = get_sampling_cap_pin();
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Channel IOs
    uint32_t channel_ios = get_first_electrode_io() | get_second_electrode_io();

    // enable
    TSC->CR = TSC_CR_TSCE;

    TSC->CR |= (TSC_CTPH_8CYCLES |
                           TSC_CTPL_10CYCLES |
                           (uint32_t)(1 << TSC_CR_SSD_Pos) |
                           TSC_SS_PRESC_DIV1 |
                           TSC_PG_PRESC_DIV16 |
                           TSC_MCV_255 |
                           TSC_SYNC_POLARITY_FALLING |
                           TSC_ACQ_MODE_NORMAL);

    // Spread spectrum
    if (0)
    {
      TSC->CR |= TSC_CR_SSE;
    }

    // Schmitt trigger and hysteresis
    TSC->IOHCR = (uint32_t)(~(channel_ios | 0 | get_sampling_cap_io()));

    // Sampling IOs
    TSC->IOSCR = get_sampling_cap_io();

    // Groups
    uint32_t grps = 0x02;
    TSC->IOGCSR = grps;

    TSC->IER &= (uint32_t)(~(TSC_IT_EOA | TSC_IT_MCE));
    TSC->ICR = (TSC_FLAG_EOA | TSC_FLAG_MCE);

}

void tsc_set_electrode(uint32_t channel_ids)
{
    TSC->IOCCR = (channel_ids);
}

void tsc_start_acq(void)
{
    TSC->CR &= ~(TSC_CR_START);

    TSC->ICR = TSC_FLAG_EOA | TSC_FLAG_MCE;

    // Set IO output to output push-pull low
    TSC->CR &= (~TSC_CR_IODEF);

    TSC->CR |= TSC_CR_START;
}

void tsc_wait_on_acq(void)
{
    while ( ! (TSC->ISR & TSC_FLAG_EOA) )
        ;
    if ( TSC->ISR & TSC_FLAG_MCE )
    {
        printf1(TAG_ERR,"Max count reached\r\n");
    }
}

uint32_t tsc_read(uint32_t indx)
{
    return TSC->IOGXCR[indx];
}

uint32_t tsc_read_button(uint32_t index)
{
    switch(index)
    {
        case 0:
            tsc_set_electrode(get_first_electrode_io());
            break;
        case 1:
            tsc_set_electrode(get_second_electrode_io());
            break;
    }
    tsc_start_acq();
    tsc_wait_on_acq();
    return tsc_read(1) < get_tsc_threshold();
}

#define PIN_SHORTED_UNDEF 0
#define PIN_SHORTED_YES 1
#define PIN_SHORTED_NO 2

int pin_grounded(int bank, int pin_mask) {
    LL_GPIO_SetPinMode(bank, (pin_mask), LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(bank, (pin_mask), LL_GPIO_PULL_UP);

    // Short delay before reading pin
    asm("nop"); asm("nop"); asm("nop"); asm("nop");

    int result = (LL_GPIO_ReadInputPort(bank) & (pin_mask)) == 0;

    LL_GPIO_SetPinPull(bank, 1, LL_GPIO_PULL_NO);

    return result ? PIN_SHORTED_YES : PIN_SHORTED_NO;
}

int tsc_sensor_exists(void)
{
    // PB1 is grounded in USB A nano & USB C touch
    static uint8_t does = PIN_SHORTED_UNDEF;
    if (does == PIN_SHORTED_UNDEF) does = pin_grounded(GPIOB, LL_GPIO_PIN_1);
    return does == PIN_SHORTED_YES;
}

int nfc_peripheral_exists(void)
{
    // USB A & USB C don't have TSC sensors and do support NFC
    if (!tsc_sensor_exists()) return 1;
    // Must be either USB A nano or USB C touch. PA8 is only grounded in USB C touch.
    static uint8_t does = PIN_SHORTED_UNDEF;
    if (does == PIN_SHORTED_UNDEF) does = pin_grounded(GPIOA, LL_GPIO_PIN_8);
    return does == PIN_SHORTED_YES;
}
