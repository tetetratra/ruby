def outer(n)
  return 'finish' if n.zero?
  bp
  inner(n, 10000)
end

def inner(n, m)
  if m.zero?
    outer(n - 1)
  else
    inner(n, m - 1)
  end
end

puts outer(10)

