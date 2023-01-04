def fib(fib_minus2, fib_minus1, n)
  if n == 1
    fib_minus1
  else
    fib(fib_minus1, fib_minus2 + fib_minus1, n - 1)
  end
end

p fib(0, 1, 20)

