def outer_loop(n)
  if n == 0
    n
  else
    inner_loop(n, 3)
  end
end

def inner_loop(n, m)
  if m.zero?
    outer_loop(n-1)
  else
    inner_loop(n, m - 1)
  end
end

