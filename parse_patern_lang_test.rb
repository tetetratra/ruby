require_relative './parse_patern_lang.rb'
# $debug = true

tests_str = <<TESTS
x->y->a->b->a->b->z->w
/y~z/k
k s x 2
0
7
---
x->y->a->b->a->b->z->w
/y~z/a/k
k s x 2
3
5
---
a->_->b
/./d
d s x 3
0
1
2
---
x->o->_->_->o->_->o->y
/o~o/_/d_
d s _ 3
2
3
5
---
main->cfunc->top->o->_->o->_->o->_->o->_->o->_->i
/o~o/o/d1
d 1 x 3
5
7
9
---
main->cfunc->top->o->_->o->_->o->_->o->_->o->_->i
/o~o/o/t_1
t 1 _ 3
5
7
9
---
main->cfunc->top->o->_->o->_->o->_->o->_->o->_->i
/o~o/o/k_
k s _ 4
4
6
8
10
---
<main>->cfunc-><top (required)>->run->eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->scan
/scan/d
d s x 8
7
8
9
10
11
12
13
14
---
<main>->cfunc-><top (required)>->run->eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->scan
/scan+/t
t s x 8
7
8
9
10
11
12
13
14
---
<main>->cfunc-><top (required)>->run->eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->scan
/scan/t
t s x 8
7
8
9
10
11
12
13
14
---
f->_->_->g
/_+/t_
t s _ 2
1
2
---
a->b->a->b->a->b->a
/b~b/b/t
t s x 1
3
---
a->b->a->b->a->b->a
/b~./t_1
t 1 _ 4
2
3
4
5
TESTS

tests = tests_str.split(/^---.*\n/).map do |test_str|
  backtrace = test_str.lines[0].chomp
  pattern   = test_str.lines[1].chomp
  expect    = test_str.lines[2..].join.chomp
  [backtrace, pattern, expect]
end

tests.each do |(bt, pat, exp)|
  result = run(bt, pat).chomp
  unless result == exp.chomp
    puts "bt: #{bt}"
    puts "pat: #{pat}"
    puts '--- expect ---'
    puts exp
    puts '--- actural ---'
    puts result
    puts '-' * 50
  end
end
