#include <usart.h>
#include <stdio.h>
#include <systick.h>


enum Color {
    Red = TIMER_CH_3,
    Green = TIMER_CH_1,
    Blue = TIMER_CH_2
};

const uint16_t PRESCALE = 540;  // with APB1=54000000: 100kHz pwm ticks
const uint16_t MAX_DUTY = 1000; // 100kHz ticks / MAX_DUTY: 100Hz pwm interval


void init_usart0() {
    usart_init(USART0, 115200);
    printf("Rainbow V2 10/2020\n\r");
}


void init_pwm_channel( uint32_t timer, uint16_t channel, timer_oc_parameter_struct *ocp ) {
    timer_channel_output_config(timer, channel, ocp);
    timer_channel_output_pulse_value_config(timer, channel, 0); // duty off
    timer_channel_output_mode_config(timer, channel, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer, channel, TIMER_OC_SHADOW_DISABLE);
}


void init_pwm( uint16_t prescale, uint16_t ticks ) {
    // Longan Nano RGB LED is connected to PA and PC pins
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOC);

    // Longan Nano RGB LED PA pins can be timer controlled via advanced function
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_1);   // green
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_2);   // blue

    // Longan Nano RGB LED PC pin is normal gpio and will be controlled by timer event routine
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_13); // red
    gpio_bit_reset(GPIOC, GPIO_PIN_13);  // switch off led

    rcu_periph_clock_enable(RCU_TIMER1); // green and blue controlled by timer 1
    timer_deinit(TIMER1);                // unset old stuff paranoia

    timer_parameter_struct tp = {
        .prescaler = prescale - 1,       // default CK_TIMER1 is 54MHz, prescale = 540 -> 100kHz ticks (shortest pulse)
        .alignedmode = TIMER_COUNTER_EDGE,
        .counterdirection = TIMER_COUNTER_UP,
        .period = ticks - 1,             // 1000 ticks for full cycle -> 100Hz pwm interval
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
    init_pwm_channel(TIMER1, TIMER_CH_1, &cp);   // green
    init_pwm_channel(TIMER1, TIMER_CH_2, &cp);   // blue
    init_pwm_channel(TIMER1, TIMER_CH_3, &cp);   // red

    // timer_update_source_config(TIMER2, TIMER_UPDATE_SRC_REGULAR);
    timer_primary_output_config(TIMER1, ENABLE);
    timer_auto_reload_shadow_enable(TIMER1);

    // PWM for non timer pins need interrupt handlers
    timer_interrupt_enable(TIMER1, TIMER_INT_CH3); // red
    timer_interrupt_enable(TIMER1, TIMER_INT_UP);
    eclic_irq_enable(TIMER1_IRQn, 1, 0);  // level and prio up to you...
    eclic_global_interrupt_enable();

    timer_enable(TIMER1);
}


// Needs to have this name to be used by the system
void TIMER1_IRQHandler() {
    static uint32_t equalizer = 0;
    uint32_t count = timer_counter_read(TIMER1);
    uint32_t limit = TIMER_CH3CV(TIMER1);
    if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP) ) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        if( !equalizer && count < limit ) {
            equalizer = 1;
            gpio_bit_reset(GPIOC, GPIO_PIN_13); // red on
        }
    }
    if( SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_CH3) ) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_CH3);
        if( equalizer && count >= limit ) {
            equalizer = 0;
            gpio_bit_set(GPIOC, GPIO_PIN_13); // red off
        }
    }
}


void set_pwm_duty( uint16_t channel, uint16_t duty ) {
    // duty >= MAX_DUTY: always on
    // duty == 0: always off
    timer_channel_output_pulse_value_config(TIMER1, channel, duty);
}


void fade( enum Color from, enum Color to ) {
    const uint32_t d = 5000000 / MAX_DUTY;      // 5ms same duty
    for( uint32_t i = 0; i <= MAX_DUTY; i++ ) { // 5s per cycle
        set_pwm_duty(from, MAX_DUTY - i);
        set_pwm_duty(to, i);
        delay_1us(d);
    }
}


int main() {
    init_usart0();
    init_pwm(PRESCALE, MAX_DUTY);

    while( 1 ) {
        fade(Red, Blue);
        fade(Blue, Green);
        fade(Green, Red);
    }
}


int _put_char(int ch) {
    return usart_put_char(USART0, ch);
}
