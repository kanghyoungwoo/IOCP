// relacy.hpp: std:: 오버라이드 없이 rl:: 네임스페이스 직접 사용
#include "relacy_lib/relacy/relacy.hpp"

struct hello_test : rl::test_suite<hello_test, 2>
{
    rl::atomic<int> x;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned idx)
    {
        if (idx == 0)
            x($).store(1, rl::mo_relaxed);
        else
            x($).load(rl::mo_relaxed);
    }
};

int main()
{
    rl::simulate<hello_test>();
    return 0;
}
