#include <inttypes.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "device.h"
#include "macros.h"
#include "adc_freerunner.h"

typedef uint16_t MasterClockTick;
typedef uint16_t SubClockTick;

const uint32_t FREQ_TICKS = 8160L*1000L/(48*4);
const MasterClockTick FREQ_MIN = 666;
const MasterClockTick FREQ_DUTY_NOMINAL = 816;

const SubClockTick B_MULTIPLIERS[] = { 48*8, 48*6, 48*4, 48*3, 48*2, 48*1, 48/2, 48/3, 48/4, 48/6, 48/8 };
const SubClockTick C_MULTIPLIERS[] = { 48*24, 48*12, 48*8, 48*4, 48*2, 48*1, 48/2, 48/4, 48/8, 48/12, 48/24 };

#define CLOCK_TYPE_A 1
#define CLOCK_TYPE_B 2
#define CLOCK_TYPE_C 3


void reset();

template<uint8_t TYPE>
class ClockGenerator {
private:
public:
  volatile SubClockTick period;
  volatile SubClockTick duty;
  volatile SubClockTick pos;
  void setPeriod(SubClockTick ticks, SubClockTick maxDuty){
    duty = min(maxDuty, ticks>>1);
    period = ticks-1;
  }
  void resetPhase(){
    pos = -1;
    on();
  }
  void clock(){
    if(++pos == duty){
      off();
    }
    if(pos > period){
      pos = 0;
      on();
    }
  }
  void toggle(){
    if(isOff())
      on();
    else
      off();
  }
  void on();
  void off();
  bool isOff();
};

template<> void ClockGenerator<CLOCK_TYPE_A>::on(){
  GENERATOR_OUTA_PORT &= ~_BV(GENERATOR_OUTA_PIN);
  reset();
}
template<> void ClockGenerator<CLOCK_TYPE_A>::off(){
  GENERATOR_OUTA_PORT |= _BV(GENERATOR_OUTA_PIN);
}
template<> bool ClockGenerator<CLOCK_TYPE_A>::isOff(){
  return GENERATOR_OUTA_PORT & _BV(GENERATOR_OUTA_PIN);
}

template<> void ClockGenerator<CLOCK_TYPE_B>::on(){
  GENERATOR_OUTB_PORT &= ~_BV(GENERATOR_OUTB_PIN);
}
template<> void ClockGenerator<CLOCK_TYPE_B>::off(){
  GENERATOR_OUTB_PORT |= _BV(GENERATOR_OUTB_PIN);
}
template<> bool ClockGenerator<CLOCK_TYPE_B>::isOff(){
  return GENERATOR_OUTB_PORT & _BV(GENERATOR_OUTB_PIN);
}

template<> void ClockGenerator<CLOCK_TYPE_C>::on(){
  GENERATOR_OUTC_PORT &= ~_BV(GENERATOR_OUTC_PIN);
}
template<> void ClockGenerator<CLOCK_TYPE_C>::off(){
  GENERATOR_OUTC_PORT |= _BV(GENERATOR_OUTC_PIN);
}
template<> bool ClockGenerator<CLOCK_TYPE_C>::isOff(){
  return GENERATOR_OUTC_PORT & _BV(GENERATOR_OUTC_PIN);
}

ClockGenerator<CLOCK_TYPE_A> clockA;
ClockGenerator<CLOCK_TYPE_B> clockB;
ClockGenerator<CLOCK_TYPE_C> clockC;

class MasterClock {
private:
public:
  volatile bool resetB = false;
  volatile bool resetC = false;
  volatile MasterClockTick period;
  MasterClockTick pos;
  void setPeriod(MasterClockTick ticks){
    period = ticks;
  }
  void clock(){
    if(++pos > period){
      pos = 0;
      clockA.clock();
      clockB.clock();
      clockC.clock();
    }
  }
  void reset(){
    if(resetB){
      clockB.resetPhase();
      resetB = false;
    }
    if(resetC){
      clockC.resetPhase();
      resetC = false;
    }
  }
};

MasterClock master;

void setup(){
  cli();
  GENERATOR_OUTA_DDR |= _BV(GENERATOR_OUTA_PIN);
  GENERATOR_OUTB_DDR |= _BV(GENERATOR_OUTB_PIN);
  GENERATOR_OUTC_DDR |= _BV(GENERATOR_OUTC_PIN);
  OCR2A = 14;
  TCCR2A |= (1 << WGM21);
  // Set to CTC Mode
  TIMSK2 |= (1 << OCIE2A);
  // Set interrupt on compare match
  // TCCR2B |= (1 << CS20); // prescalar 1
  TCCR2B |= (1 << CS21); // prescalar 8
  // TCCR2B |= (1 << CS22); // prescalar 64
  setup_adc();
  sei();
}

SubClockTick mulB = 5;
SubClockTick mulC = 5;
void loop(){
  MasterClockTick a;
  SubClockTick b, c;
  b = (getAnalogValue(GENERATOR_RATE_B_CONTROL) * 11) / ADC_VALUE_RANGE;
  if(b != mulB){
    mulB = b;
    master.resetB = true;
  }
  c = (getAnalogValue(GENERATOR_RATE_C_CONTROL) * 11) / ADC_VALUE_RANGE;
  if(c != mulC){
    mulC = c;
    master.resetC = true;
  }
  a = getAnalogValue(GENERATOR_RATE_A_CONTROL)*2/3 + FREQ_MIN;
  uint32_t ticks = FREQ_TICKS / a;
  uint16_t maxDuty = FREQ_DUTY_NOMINAL / ticks;
  master.setPeriod(ticks);
  clockA.setPeriod(48, maxDuty);
  clockB.setPeriod(B_MULTIPLIERS[b], maxDuty);
  clockC.setPeriod(C_MULTIPLIERS[c], maxDuty);
}

ISR(TIMER2_COMPA_vect){
  master.clock();
}

void reset(){
  master.reset();
}
