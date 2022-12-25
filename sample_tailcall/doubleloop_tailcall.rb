def o(n)
  if n == 0
    n
  else
    i(n, 5)
  end
end

def i(n, m)
  if m.zero?
    o(n-1)
  else
    i(n, m - 1)
  end
end

