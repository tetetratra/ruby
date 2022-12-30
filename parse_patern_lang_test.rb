require_relative './parse_patern_lang.rb'
# $debug = true

tests_str = <<TESTS
x->o->_->_->o->_->o->y
/o~o/_/d
d s 3
2
3
5
---
@main->@cfunc->@top->o->_->o->_->o->_->o->_->o->_->@i
/o~o/o/d
d s 3
5
7
9
---
@main->@cfunc->@top->o->_->o->_->o->_->o->_->o->_->@i
/o~o/o/t
t s 3
5 5
7 7
9 9
---
@<main>->@cfunc->@<top (required)>->run->@eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->@scan
/scan/t
t s 7
7 7
8 8
9 9
10 10
11 11
12 12
13 13
---
@<main>->@cfunc->@<top (required)>->run->@eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->@scan
/scan+/t
t s 1
7 13
---
@<main>->@cfunc->@<top (required)>->run->@eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->@scan
/scan/t
t s 7
7 7
8 8
9 9
10 10
11 11
12 12
13 13
---
f->_->_->g
/./d
d s 2
0
3
---
f->_->_->g
/_/d
d s 2
1
2
TESTS

tests = tests_str.split("---\n").map do |test_str|
  backtrace = test_str.lines[0].chomp
  pattern   = test_str.lines[1].chomp
  expect    = test_str.lines[2..].join.chomp
  [backtrace, pattern, expect]
end

tests.each do |(bt, pat, exp)|
  result = run(bt, pat)
  unless result == exp.chomp
    puts "bt: #{bt}"
    puts "pat: #{pat}"
    puts '--- expected ---'
    puts exp
    puts '--- actural ---'
    puts result
    puts '-' * 50
  end
end
