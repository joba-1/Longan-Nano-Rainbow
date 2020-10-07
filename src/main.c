#include <usart.h>
#include <stdio.h>
#include <systick.h>


enum Color {
    Red,
    Green,
    Blue
};

const uint32_t MAX_DUTY = 1000;


void init_usart0() {
    usart_init(USART0, 115200);
    printf("Rainbow V1 10/2020\n\r");
}


volatile uint32_t _ch3 = 0;
volatile uint32_t _no3 = 0;


void TIMER1_IRQHandler() {
    if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_CH0) ) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_CH0);
        if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP) ) {
            timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        }
    }
    else if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_CH1) ) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_CH1);
        if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP) ) {
            timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        }
    }
    else if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_CH2) ) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_CH2);
        if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP) ) {
            timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        }
    }
    else if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_CH3) ) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_CH3);
        if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP) ) {
            timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        }
        else {
            if( _ch3 <= _no3 ) {
                gpio_bit_set(GPIOC, GPIO_PIN_13);
                _ch3++;
            }
        }
    }
    else if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP) ) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        if( _no3 <= _ch3 ) {
            gpio_bit_reset(GPIOC, GPIO_PIN_13);
            _no3++;
        }
    }
}


void init_channel( uint32_t timer, uint16_t channel, timer_oc_parameter_struct *ocp ) {
    timer_channel_output_config(timer, channel, ocp);
    timer_channel_output_pulse_value_config(timer, channel, 0); // duty off
    timer_channel_output_mode_config(timer, channel, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer, channel, TIMER_OC_SHADOW_DISABLE);
}


void init_pwm() {
    // Longan Nano RGB LED is connected to PA and PC pins
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOC);

    // Longan Nano RGB LED PA pins can be timer controlled via advanced function
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_1); // green
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_2); // blue

    // Longan Nano RGB LED PC pin is normal gpio and will be controlled by timer event routine
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_13); // red
    gpio_bit_reset(GPIOC, GPIO_PIN_13); // switch off led

    rcu_periph_clock_enable(RCU_TIMER1); // green and blue controlled by timer 1
    timer_deinit(TIMER1);                // unset old stuff paranoia

    timer_parameter_struct tp = {
        .prescaler = 540 - 1, // default CK_TIMER1 is 54MHz -> timer ticks = 100kHz (shortest pulse)
        .alignedmode = TIMER_COUNTER_EDGE,
        .counterdirection = TIMER_COUNTER_UP,
        .period = MAX_DUTY - 1, // 1000 full cycle ticks -> frequency = 100Hz
        .clockdivision = TIMER_CKDIV_DIV1,
        .repetitioncounter = 0};
    timer_init(TIMER1, &tp);

    timer_oc_parameter_struct cp = {
        .outputstate = TIMER_CCX_ENABLE,
        .outputnstate = TIMER_CCXN_DISABLE,
        .ocpolarity = TIMER_OC_POLARITY_LOW, // RGB led output is inverted
        .ocnpolarity = TIMER_OCN_POLARITY_LOW,
        .ocidlestate = TIMER_OC_IDLE_STATE_HIGH,
        .ocnidlestate = TIMER_OCN_IDLE_STATE_HIGH};
    init_channel(TIMER1, TIMER_CH_1, &cp);  // green
    init_channel(TIMER1, TIMER_CH_2, &cp);  // blue
    init_channel(TIMER1, TIMER_CH_3, &cp);  // red

    timer_update_source_config(TIMER2, TIMER_UPDATE_SRC_REGULAR);
    timer_primary_output_config(TIMER1, ENABLE);
    timer_auto_reload_shadow_enable(TIMER1);
    timer_interrupt_enable(TIMER1, TIMER_INT_CH3);
    timer_interrupt_enable(TIMER1, TIMER_INT_UP);
    timer_enable(TIMER1);
}


void set_duty( enum Color color, uint32_t duty ) {
    // duty >= MAX_DUTY: always on
    // duty == 0: always off
    if( color == Green ) {
        timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_1, duty);
    }
    else if( color == Blue ) {
        timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_2, duty);
    }
    else if( color == Red ) {
        timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, duty);
    }
}


void fade( enum Color from, enum Color to ) {
    const uint32_t d = 10000000 / MAX_DUTY; // 1s per cycle
    for( uint32_t i = 0; i <= MAX_DUTY; i++ ) {
        set_duty(from, MAX_DUTY - i);
        set_duty(to, i);
        delay_1us(d);
    }
}


int main() {
    eclic_global_interrupt_enable();
    eclic_set_nlbits(ECLIC_GROUP_LEVEL3_PRIO1);
    eclic_irq_enable(TIMER1_IRQn, 1, 0);

    init_usart0();
    init_pwm();

    while( 1 ) {
        fade(Red, Blue);
        fade(Blue, Green);
        fade(Green, Red);
    }
}


int _put_char(int ch) {
    return usart_put_char(USART0, ch);
}
