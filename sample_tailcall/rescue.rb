def f
  g(1000)
rescue
  p caller_locations
end

def g(n)
  raise if n == 1
  g(n-1)
end

f
