#### Extension to the Shunting Yard algorithm to allow variable numbers of arguments to functions
---
Copyright Robin Sheat (2008) as CC BY-SA 3.0 NZ [(source)](https://blog.kallisti.net.nz/2008/02/extension-to-the-shunting-yard-algorithm-to-allow-variable-numbers-of-arguments-to-functions/)

The standard shunting yard algorithm can handle functions, but with the restriction that the number of arguments to them is known (really, this is a limitation of the RPN algorithm, but it’s at this point in the process that we need to deal with the problem). In this case, I wanted to handle functions with a variable number of arguments, so `max(1,2)` would work, and so would `max(1,2,3,4,5)`. To do this, I extended the standard algorithm. 

Below is the algorithm from Wikipedia. My additions are in bold. This requires two more stacks, a `were values` stack, and an `arg count` stack. It also requires that you can attach the number of arguments to an instance of a function. In my case, I did it with a small class that took the function and an argument count, with one of these created during tokenisation for each function encountered.

* While there are tokens to be read:
    * Read a token.
    * If the token is a number, then add it to the `output` queue. **If the `were values` stack has a value on it, pop it and push `true`.**
    * If the token is a function token, then push it onto the stack. **Push `0` onto the `arg count` stack. If the `were values` stack has a value on it, pop it and push `true`. Push `false` onto `were values`.**
    * If the token is a function argument separator (e.g., a comma):
        * Until the topmost element of the stack is a left parenthesis, pop the element onto the `output` queue. If no left parentheses are encountered, either the separator was misplaced or parentheses were mismatched. **Pop `were values` into `w`. If `w` is `true`, pop `arg count` into `a`, increment `a` and push back into `arg count`. Push `false` into were values.**
    * If the token is an operator `o1` then:
        * while there is an operator `o2` at the top of the stack, and either `o1` is associative or left-associative and its precedence is less than (lower precedence) or equal to that of `o2`, or `o1` is right-associative and its precedence is less than (lower precedence) that of `o2`: pop `o2` off the stack, onto the `output` queue
        * push `o1` onto the operator stack.
    * If the token is a left parenthesis, then push it onto the stack.
    * If the token is a right parenthesis:
        *  Until the token at the top of the stack is a left parenthesis, pop operators off the stack onto the `output` queue.
        *  Pop the left parenthesis from the stack, but not onto the `output` queue.
        *  If the token at the top of the stack is a function token
            *  **Pop stack into `f`**
            *  **Pop `arg count` into `a`**
            *  **Pop `were values` into `w`**
            *  **If `w` is `true`, increment `a`**
            *  **Set the argument count of `f` to `a`**
            *  **Push `f` onto `output` queue**
        * If the stack runs out without finding a left parenthesis, then there are mismatched parentheses.
*  When there are no more tokens to read:
    *  While there are still operator tokens in the stack:
        * If the operator token on the top of the stack is a parenthesis, then there are mismatched parenthesis.
        * Pop the operator onto the `output` queue.
* Exit.

Note that because I didn’t feel like correctly listifying most of it, consider an if to apply to the end of that sentence only. Operation order is usually important.

With this done, the part of the RPN algorithm that says
> It is known that the function takes n arguments.

can now be satisfied.
