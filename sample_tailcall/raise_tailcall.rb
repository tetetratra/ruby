def f(n)
  if n.zero?
    raise
  else
    f(n-1)
  end
end
