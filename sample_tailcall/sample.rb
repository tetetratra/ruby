def f()
  a() # 末尾呼び出し
end

def g(c)
  a()
  if c
    b() # 末尾呼び出し
  else
    b() + 1
  end
end

def a()
end
def b()
  puts caller_locations(0)
end

g(true)


