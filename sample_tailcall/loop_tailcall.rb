def start
  loop(1)
end

def loop(n)
  if n == 200
    finish()
  else
    loop(n + 1)
  end
end

def finish
  raise
end
