def o(n)
  if n == 300
    puts 'FINISH'
  else
    i(n, 1)
  end
end

def i(n, m)
  raise if n == 123 && m == 456

  if m == 500
    o(n + 1)
  else
    i(n, m + 1)
  end
end

o(1)

