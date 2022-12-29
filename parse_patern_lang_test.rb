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
  ]
].each do |(s, c, e)|
  r = run(s, c)
  unless r == e.chomp
    puts "arg: #{s}"
    puts "cmd: #{c}"
    puts '--- expected ---'
    puts e
    puts '--- actural ---'
    puts r
  end
end
