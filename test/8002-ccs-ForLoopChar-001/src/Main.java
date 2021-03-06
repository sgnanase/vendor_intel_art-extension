
/**
*
* Expected result: no sinking because char type is not supported
*
**/

public class Main {                                                                                                                                                   
    final int iterations = 10;

    public int testLoopAdd(char n) {
        char testVar = '0';
        int additionalVar = 48;
        for (int i = 0; i < iterations; i++) {
              testVar += 3;
              additionalVar += (i*2)%5 + n;
        }
        return testVar + additionalVar;
    }

    public int testLoopSub(char n) {
        char testVar = '0';
        int additionalVar = 48;
        for (int i = 0; i < iterations; i++) {
              testVar -= 3;
              additionalVar += (i*2)%5 + n;
        }
        return testVar + additionalVar;
    }

    public int testLoopMul(char n) {
        char testVar = '0';
        int additionalVar1 = 48;
        int additionalVar2 = 2;
        int additionalVar3 = 3;
        int additionalVar4 = 4;
        int additionalVar5 = 5;
        for (int i = 0; i < 3; i++) {
              testVar *= 3;
              additionalVar1 += (i*2)%5;
              additionalVar2 += (i*2)%5 + i;
              additionalVar3 += (i*2)%5 + additionalVar1/2 + additionalVar3 ;
              additionalVar4 += (i*2)%5 - additionalVar4%2;
              additionalVar5 += (i*2)%5 - additionalVar2 + n;
        }
        return testVar + additionalVar1 + additionalVar2 + additionalVar3 + additionalVar4 + additionalVar5;
    }

    public int testLoopDiv(char n) {
        char testVar = '0';
        int additionalVar = 48;
        for (int i = 0; i < iterations; i++) {
              testVar /= 3;
              additionalVar += (i*2)%5 + n;
        }
        return testVar + additionalVar;
    }

    public int testLoopRem(char n) {
        char testVar = '0';
        int additionalVar = 48;
        for (int i = 0; i < iterations; i++) {
              testVar %= 3;
              additionalVar += (i*2)%5 + n;
        }
        return testVar + additionalVar;
    }


    public static void main(String[] args)
    {
         System.out.println(new Main().testLoopAdd((char)10));
         System.out.println(new Main().testLoopSub((char)10));
         System.out.println(new Main().testLoopMul((char)10));
         System.out.println(new Main().testLoopDiv((char)10));
         System.out.println(new Main().testLoopRem((char)10));
    }

}  

