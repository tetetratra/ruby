def a(an, bn, cn)
  if an == 1
    puts 'FINISH'
  else
    b(an, bn, cn)
  end
end

def b(an, bn, cn)
  if bn == 1
    a(an - 1, bn, cn)
  else
    c(an, bn, cn)
  end
end

def c(an, bn, cn)
  if cn == 1
    b(an, bn - 1, cn)
  else
    c(an, bn, cn - 1)
  end
end
