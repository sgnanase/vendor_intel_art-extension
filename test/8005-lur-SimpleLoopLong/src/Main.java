
public class Main
{
    public class A 
    {
        public long value;
    }

    public long testLoop()
    {
        A x;
        x = new A();
        long a0 = 0x7000000000000007L;

        for (int i = 0; i < 10; i++)
        {
            a0++;
            x.value = a0;
        }

        return x.value;
    }

    public void test()
    {
        System.out.println(testLoop());
    }

    public static void main(String[] args)
    {
        new Main().test();        
    }    
}
