def foo(n)
  raise if n.zero?

  if rand(100).zero?
    rare(n-1)
  else
    bar(n-1)
  end
end

def bar(n)
  raise if n.zero?
  baz(n-1)
end

def baz(n)
  raise if n.zero?
  foo(n-1)
end

def rare(n)
  foo(n)
end

