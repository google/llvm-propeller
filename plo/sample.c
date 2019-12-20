/* sample.c */
volatile int count;

static int goose() {
  return 13;
}

__attribute__((noinline))
double this_is_very_code(double tt) {
  volatile double dead = 3434343434, beaf = 56565656; /* Avoid compiler optimizing away */
  return dead / beaf + beaf / dead + tt / 183;
}

__attribute__((noinline))
int compute_flag(int i)
{
        if (i % 10 < 4)          /* ... in 40% of the iterations */
                return i + 1;
        return 0;
}

int sample1_func();

int main(void)
{
        int i;
        int flag;
        volatile double x = 1212121212, y = 121212; /* Avoid compiler optimizing away */

        for (i = 0; i < 2000000000; i++) {
                flag = compute_flag(i);
        
                /* Some other code */
                count++;

                if (flag)
                        x += x / y + y / x;     /* Execute expensive division if flag is set */
		if (count % 137949234 == 183) {
		  x += this_is_very_code(count) + sample1_func();
		}
		  
        }
        return goose();
}
