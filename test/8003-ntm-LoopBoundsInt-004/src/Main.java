

// No non temporal move because number of iterations is less than optimization threshold


public class Main {                                                                                                                                                   
    final int iterations = 131072-1; //Less than optimization threshold

    public long checkSum(int[] tab, int n) {
        long s = 0;
        for (int i = 0; i < n ; i++) {
            s = s + tab[i];
        }
        return s;
    }

    public long testLoop(int[] tab) {
        for (int i = 0; i < iterations ; i++) {
            tab[i] = i;
        }

        return checkSum(tab, iterations);
    }

    public void test()
    {
        int[] tab = new int [iterations];
        System.out.println(testLoop(tab));
    }

    public static void main(String[] args)
    {
        new Main().test();
    }

}  

