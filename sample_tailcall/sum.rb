def sum(n, s)
  if n.zero?
    s
  else
    sum(n - s, s + 1)
  end
end

# p sum(15, 0) = sum(15, 1) = sum(14, 2) = sum(12, 3) = sum(9, 4) = sum(5, 5) = sum(0, 6)

# def sum_r(n, s)
#   if s.zero?
#     n
#   else
#     sum_r(n + s, s - 1)
#   end
# end
# n = 6; sum_r(0, n) - n

