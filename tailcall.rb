def b1(n)
  if n == 1
    c()
  else
    b2(n-1)
  end
end

def b2(n)
  if n == 1
    c()
  else
    b3(n-1)
  end
end

def b3(n)
  if n == 1
    c()
  else
    b4(n-1)
  end
end

def b4(n)
  if n == 1
    c()
  else
    b5(n-1)
  end
end

def b5(n)
  if n == 1
    c()
  else
    b1(n-1)
  end
end
