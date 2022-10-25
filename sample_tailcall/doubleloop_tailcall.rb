def o(n)
  if n == 1
    puts 'FINISH'
  else
    i(n, 500)
  end
end

def i(n, m)
  if m == 1
    o(n-1)
  else
    i(n, m - 1)
  end
end

