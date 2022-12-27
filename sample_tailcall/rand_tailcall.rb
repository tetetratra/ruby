def f(n)
  if n == 1
    raise
  elsif n == 100
    if rand(2).zero?
      a(n-1)
    else
      b(n-1)
    end
  else
    f(n-1)
  end
end

def a(n)
  f(n)
end

def b(n)
  f(n)
end
