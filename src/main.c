/*
Example program to setup pwm on a Longan Nano board with a Risc V processor GD32VF103
to produce a rainbow color cycle with the on board rgb led.
It uses purely hardware driven alternate function pins and an interrupt driven approach.
Hardware driven approach is faster and uses no CPU cycles but only works on selected pins.
Interrupt driven approach can be used on any pin but is limited to at least 10 times lower frequencies.
*/

#include <usart.h>
#include <stdio.h>
#include <systick.h>


// PWM timimg stuff
const uint16_t PRESCALE = 216;  // with APB1=54000000: 540 = 100kHz pwm ticks. Min 200 for interrupt pins.
const uint16_t MAX_DUTY = 1000; // 100kHz ticks/MAX_DUTY: 100Hz pwm interval


// Application timing stuff
const uint32_t DUTY_US = 5000; // 5ms same duty: duty*MAX_DUTY = 5s per fade

// Pins the application uses for the led colors
enum Color {
    Red   = TIMER_CH_3,
    Green = TIMER_CH_1,
    Blue  = TIMER_CH_2
};


/* 
Init serial and reroute c standard output there.
Convenience stuff, not related to pwm at all.
*/

// Init serial output and announce ourselves there
void init_usart0() {
    usart_init(USART0, 115200);
    printf("Rainbow V2 10/2020\n\r");
}

// Reroute c standard output to serial
int _put_char(int ch) {
    return usart_put_char(USART0, ch);
}


/* 
Setup hardware supported pwm using a timer.
Since not all pins can be made to follow the timer generated pattern directly, 
interrupts are used to switch gpio state following the pwm pattern.
Interrupt based pwm maxes out at least 10 times earlier and uses cpu cycles. 
So try to avoid, e.g. connect the red led pin PC13 to adjacent pin PA0 would be an option.
*/

// Make a timer channel use pwm pattern defined by given structure
void init_pwm_channel( uint32_t timer, uint16_t channel, timer_oc_parameter_struct *ocp ) {
    timer_channel_output_config(timer, channel, ocp);
    timer_channel_output_pulse_value_config(timer, channel, 0); // duty off
    timer_channel_output_mode_config(timer, channel, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer, channel, TIMER_OC_SHADOW_DISABLE);
}

// Setup timer to generate pwm ticks and interval,
// use channels to define the pwm pattern, 
// and make gpio pins follow that pattern
void init_pwm( uint16_t prescale, uint16_t ticks ) {
    // Longan Nano RGB LED is connected to PA and PC pins
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOC);

    // Longan Nano RGB LED PA pins can be timer controlled via advanced function
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_1);   // green
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_2);   // blue

    // Longan Nano RGB LED PC pin is normal gpio and will be controlled by timer event routine
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_13); // red
    gpio_bit_set(GPIOC, GPIO_PIN_13);  // switch off inverted led

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
    eclic_global_interrupt_enable(); // Make the interrupt controller do its work

    timer_enable(TIMER1);
}

// Timer event routine called on rising and falling edges of the pwm signal
// Tested to work up until around 200kHz tick speed (makes 200Hz intervals with 1000 steps)
// Needs to have this name to be used by the system
void TIMER1_IRQHandler() {
    // Interrupts not only used for signal going up or down,
    // so filter depending on current output state and timer value
    static uint32_t equalizer = 0; // dont act if state already ok
    uint32_t count = timer_counter_read(TIMER1);
    uint32_t limit = timer_channel_capture_value_register_read(TIMER1, TIMER_CH_3);

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

// From the constant pwm interval duration of MAX_DUTY ticks,
// how many of them is the signal of a channel up?
void set_pwm_duty( uint16_t channel, uint16_t duty ) {
    // duty >= MAX_DUTY: always on
    // duty == 0: always off
    timer_channel_output_pulse_value_config(TIMER1, channel, duty);
}


/* Application side using the pwm to fade LEDs */

// Gradualy adjust pwm duty to make LED darker or brighter 
void fade( enum Color from, enum Color to, uint32_t duty_us ) {
    for( uint32_t i = 0; i <= MAX_DUTY; i++ ) { // 5s per cycle
        set_pwm_duty(from, MAX_DUTY - i);
        set_pwm_duty(to, i);
        delay_1us(duty_us);
    }
}

// Putting it all together: 
// * Start program saying hello on serial
// * Setup the pwm signal
// * Start fading between colors to cycle through the rainbow
int main() {
    init_usart0();
    init_pwm(PRESCALE, MAX_DUTY);

    while( 1 ) {
        fade(Red,   Blue, DUTY_US);
        fade(Blue, Green, DUTY_US);
        fade(Green,  Red, DUTY_US);
    }
}
