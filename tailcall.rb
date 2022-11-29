def b(n)
  if n == 1
    c(5)
  else
    b(n-1)
  end
end

def c(n)
  if n == 1
    d()
  else
    c(n-1)
  end
end
