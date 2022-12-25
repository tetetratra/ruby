Plus = Struct.new(:body) do
  def inspect
    "#{body.inspect}+"
  end
end

Mul = Struct.new(:body) do
  def inspect
    "#{body.inspect}*"
  end
end

Times = Struct.new(:body, :times) do
  def inspect
    "#{body.inspect}{#{times}}"
  end
end

Hat = Struct.new(:_) do
  def inspect
    '^'
  end
end

Doller = Struct.new(:_) do
  def inspect
    '$'
  end
end

class Range
  def shift(n)
    (self.begin + n)..(self.end + n)
  end
end

Call = Struct.new(:name) do
  def inspect
    "@#{name.inspect}"
  end
end

Tailcall = Struct.new(:name) do
  def inspect
    name.inspect
  end
end

def run(string_raw, pattern_exp)
  string = string_raw.split.map do |s|
    if s.start_with?('@')
      Call.new(s.sub('@', ''))
    else
      Tailcall.new(s)
    end
  end
  p string if $debug
  pattern_exp.split('/') => [_discard_empty, *patterns, command]
  filtered = filter(string, patterns)

  s = "#{command[0]}#{command[1] || 's'}\n"
  case command[0] # 1文字目
  when 'd'
    s += filtered.map do |(indexes, _str, skips)|
      if $debug
        p _str, skips
      end
      (indexes - skips).join("\n")
    end.join("\n")
    s
  when 'k'
    all = (0..(string.size - 1)).to_a
    filtered.each do |(indexes, _str, skips)|
      all -= indexes
      all += skips # kのときの\は「マッチするが残す」にする
    end
    s += all.join("\n")
    s
  when 't'
    # 'a a a' '/a \a a/t' が `0 2` ではなく `0 0\n2 2` なるように頑張っている
    p filtered
    s += filtered.map do |(indexes, str, skips)|
      indexes.zip(str)
        .chunk { |(i, _)| !skips.include?(i) } # skipを消す
        .select(&:first).map(&:last)
        .map do |split_by_skip|

        split_by_skip
          .chunk { |(_i, call)| Tailcall === call } # Tailcallを消す
          .select(&:first).map(&:last)
          .map { _1.map(&:first) } # indexのみになるように整形
          .map { format('%d %d', _1[0], _1[-1]) }
          .join("\n")
      end.join("\n")
    end.join("\n")
    s.gsub(/\n+/, "\n") # FIXME: 空のjoinで無駄に増えた分をに消す(応急処置)
  end
rescue => e
  STDERR.puts e.full_message
  ''
  "d\n"
end

def filter(init_string, patterns)
  init_indexes = (0..(init_string.size - 1)).to_a

  patterns.reduce([[init_indexes, init_string, []]]) do |memo, pattern_code|
    memo.flat_map do |(indexes, string, skips)|
      pattern_ast = parse(pattern_code)
      p pattern_ast if $debug

      pattern_bytecode = compile(pattern_ast)
      p pattern_bytecode if $debug

      scan(pattern_bytecode, string).map do |last_vm|
        range = last_vm[:sp_from]..last_vm[:sp]
        [
          range.map { |i| i + init_indexes.first },
          string[range],
          last_vm[:skips]
        ]
      end
    end
  end
end

MACRO = {
  'RECENT' => -> _s {
    [Times.new('.', 3), Doller.new]
  },
  'BEGINNING' => -> _s {
    [Hat.new, Times.new('.', 3)]
  },
  '[a-z_<>]+~[a-z_<>]+' => -> s {
    from, to = s.split('~')
    ["\\#{from}", Mul.new('.'), "\\#{to}"]
  }
}

def parse(pattern_str)
  macro_regex = MACRO.keys.join('|')
  parsed = pattern_str.scan(%r@#{macro_regex}|{\d+}|\^|\$|\.|[A-Z]+|(?<= )_|_(?= )|\\?[a-z_<>]+|\+/d|\*/d|\+|\*|\(|\)|_|~@)
  pattern = []
  until parsed.empty?
    poped = parsed.shift
    case poped
    when ')'
      i = pattern.rindex('(')
      pattern = i.zero? ? [pattern[(i+1)..]] : [*pattern[0..(i-1)], pattern[(i+1)..]]
    when '+'
      pattern << Plus.new(pattern.pop)
    when '*'
      pattern << Mul.new(pattern.pop)
    when /{(\d+)}/
      pattern << Times.new(pattern.pop, $1.to_i)
    when '^'
      pattern << Hat.new
    when '$'
      pattern << Doller.new
    else
      if matched_macro = MACRO.find { |(regex, _replace)| Regexp.new(regex).match?(poped) }
        pattern.concat matched_macro[1].call(poped)
      else
        pattern << poped
      end
    end
  end
  pattern
end

def compile(pattern)
  compile_r = ->(pat) do
    case pat
    in String
      ["char #{pat}"]
    in Array
      pat.map { |p| compile_r.(p) }.flatten(1)
    in Plus
      compiled = compile_r.(pat.body)
      [
        *compiled,
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    in Mul
      compiled = compile_r.(pat.body)
      [
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    in Times
      compile_r.(pat.body) * pat.times
    in Hat
      ['hat']
    in Doller
      ['doller']
    end
  end
  [*compile_r.(pattern), 'match']
end

def scan(code, string)
  from = 0
  Enumerator.produce do
    last_vm = exec(code, string, from)
    raise StopIteration if last_vm.nil?

    range = last_vm[:sp_from]..last_vm[:sp]
    if range.begin > range.end
      from = last_vm[:sp_from] + 1
      STDERR.puts 'maybe bug'
      nil
    else
      from = last_vm[:sp] + 1
      last_vm
    end
  end.to_a.compact
end

def exec(codes, string, from = 0)
  # sp: 0,1,2... から始めることで、前方部分一致をサポート
  init_vm = (from..(string.size - 1)).map { |i| { sp: i, pc: 0, sp_from: i, skips: [] } }.reverse

  (0..).reduce(init_vm) do |vm, _|
    break nil if vm.empty?

    vm => [*, { sp:, pc:, sp_from:, skips: }] # sp: string pointer
    code = codes[pc]

    if $debug
      puts
      p vm, code
    end

    case code
    when /^char (.*)/
      method = string[sp]&.name
      p method if $debug
      next vm[..-2] if method.nil?

      c = $1
      skip = nil
      if c.start_with?('\\')
        c = c.sub('\\', '')
        skip = sp
      end

      if method == c || c == '.'
        next [*vm[..-2], { sp: sp + 1, pc: pc + 1, sp_from:, skips: [*skips, skip].compact }]
      else
        next vm[..-2]
      end
    when /^hat/
      if sp == 0
        next [*vm[..-2], { sp: sp, pc: pc + 1, sp_from:, skips: }]
      else
        next vm[..-2]
      end
    when /^doller/
      if sp == string.size
        next [*vm[..-2], { sp: sp, pc: pc + 1, sp_from:, skips: }]
      else
        next vm[..-2]
      end
    when /^split (-?\d+) (-?\d+)/
      j1 = $1.to_i
      j2 = $2.to_i
      next [
        *vm[..-2],
        { sp: sp, pc: pc + 1,  sp_from:, skips: },
        { sp: sp, pc: pc + j2, sp_from:, skips: },
        { sp: sp, pc: pc + j1, sp_from:, skips: } # 最長一致のため、繰り返す方(j1)を先に試す
      ]
    when /^jump (-?\d+)/
      j = $1.to_i
      next [*vm[..-2], { sp: sp, pc: pc + j, sp_from:, skips: }]
    when /^match/
      break { sp: sp - 1, pc:, sp_from:, skips: }
    end
  end
end

if $PROGRAM_NAME == __FILE__
  $debug = true
  puts run(ARGV[0], ARGV[1])
end
