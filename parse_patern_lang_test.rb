require_relative './parse_patern_lang.rb'
# $debug = true

[
  [
    'x->o->_->_->o->_->o->y',
    '/o~o/_/d',
    <<~EXPECT
      d s 3
      2
      3
      5
    EXPECT
  ],
  [
    '@main->@cfunc->@top->o->_->o->_->o->_->o->_->o->_->@i',
    '/o~o/o/d',
    <<~EXPECT
      d s 3
      5
      7
      9
    EXPECT
  ],
  [
    '@main->@cfunc->@top->o->_->o->_->o->_->o->_->o->_->@i',
    '/o~o/o/t',
    <<~EXPECT
      t s 3
      5 5
      7 7
      9 9
    EXPECT
  ],
  [
    '@<main>->@cfunc->@<top (required)>->run->@eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->@scan',
    '/scan/t',
    <<~EXPECT
      t s 7
      7 7
      8 8
      9 9
      10 10
      11 11
      12 12
      13 13
    EXPECT
  ],
  [
    '@<main>->@cfunc->@<top (required)>->run->@eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->@scan',
    '/scan+/t',
    <<~EXPECT
      t s 1
      7 13
    EXPECT
  ],
  [
    "@<main>->@cfunc->@<top (required)>->run->@eval->eval->define_variable->scan->scan->scan->scan->scan->scan->scan->@scan",
    "/scan/t",
    <<~EXPECT
    t s 7
    7 7
    8 8
    9 9
    10 10
    11 11
    12 12
    13 13
    EXPECT
  ]
].each do |(s, c, e)|
  r = run(s, c)
  unless r == e.chomp
    puts
    puts "arg: #{s}"
    puts "cmd: #{c}"
    puts '--- expected ---'
    puts e
    puts '--- actural ---'
    puts r
  end
end
