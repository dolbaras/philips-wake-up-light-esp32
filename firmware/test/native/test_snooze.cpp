#include "snooze.h"
#include <cstdio>
using namespace sched;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++fails;} }while(0)
int main(){
  SnoozeController s;
  CHECK(s.state()==RingState::Idle);
  // снуз без звонка игнорируется
  CHECK(!s.onSnoozePressed(100,10));
  // звонок -> снуз -> через 10 мин повтор
  s.onAlarmFired(100); CHECK(s.state()==RingState::Ringing);
  CHECK(s.onSnoozePressed(100,10)); CHECK(s.state()==RingState::Snoozed);
  CHECK(!s.tick(105));                 // ещё рано
  CHECK(!s.tick(109));
  CHECK(s.tick(110));                  // истёк -> снова звенит
  CHECK(s.state()==RingState::Ringing);
  // можно снузить повторно
  CHECK(s.onSnoozePressed(110,5)); CHECK(s.tick(115));
  // dismiss завершает
  s.onDismiss(); CHECK(s.state()==RingState::Idle); CHECK(!s.tick(200));
  printf(fails?"\n%d FAILED\n":"\nСНУЗ: все проверки прошли\n",fails);
  return fails?1:0;
}
