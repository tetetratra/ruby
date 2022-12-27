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

BackRef = Struct.new(:body) do
  def inspect
    "\\#{body}"
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
  _discard_empty, *patterns, command = pattern_exp.split('/')
  filtered = filter(string, patterns)

  case command[0] # 1文字目
  when 'd'
    s = filtered.map do |(indexes, _str, skips)|
      if $debug
        p _str, skips
      end
      (indexes - skips).join("\n")
    end.join("\n")
  when 'k'
    all = (0..(string.size - 1)).to_a
    filtered.each do |(indexes, _str, skips)|
      all -= indexes
      all += skips # kのときの\は「マッチするが残す」にする
    end
    s = all.join("\n")
  when 't'
    # 'a a a' '/a \a a/t' が `0 2` ではなく `0 0\n2 2` なるように頑張っている
    p filtered
    s = filtered.map do |(indexes, str, skips)|
      indexes.zip(str)
        .chunk { |(i, _)| !skips.include?(i) } # skipを消す
        .select(&:first).map(&:last)
        .map do |split_by_skip|

        split_by_skip
          .chunk { |(_i, call)| Tailcall === call } # Tailcallを消す
          .select(&:first).map(&:last)
          .map { |e| e.map(&:first) } # indexのみになるように整形
          .map { |e| format('%d %d', e[0], e[-1]) }
          .join("\n")
      end.join("\n")
    end.join("\n").gsub(/\n+/, "\n").gsub(/^\n+|\n+$/, '')
  end
  header = "#{command[0]} #{command[1] || 's'} #{s.lines.size}"
  "#{header}\n#{s}"
rescue => e
  STDERR.puts e.full_message
  ''
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
  parsed = pattern_str.scan(%r@#{macro_regex}|\\\d|{\d+}|\^|\$|\.|[A-Z]+|(?<= )_|_(?= )|\\?[a-z_<>]+|\+/d|\*/d|\+|\*|\(|\)|_|~@)
  pattern = []
  until parsed.empty?
    poped = parsed.shift
    case poped
    when ')'
      i = pattern.rindex('(')
      pattern = if i.zero?
                  [pattern[(i+1)..nil]]
                else
                  [*pattern[0..(i-1)], pattern[(i+1)..nil]]
                end
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
    when /\\(\d)/
      pattern << BackRef.new($1.to_i)
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
  refi = -1 # ref_to 0 は実行中は現れない(matchが先にくるので)

  compile_r = ->(pat) do
    case pat
    when String
      ["char #{pat}"]
    when Array
      refi += 1
      [
        refi.zero? ? nil : "ref_from #{refi}",
        *pat.map { |p| compile_r.(p) }.flatten(1),
        refi.zero? ? nil : "ref_to #{refi}",
      ].compact
    when BackRef
      ["backref #{pat.body}"]
    when Plus
      compiled = compile_r.(pat.body)
      [
        *compiled,
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    when Mul
      compiled = compile_r.(pat.body)
      [
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    when Times
      compile_r.(pat.body) * pat.times
    when Hat
      ['hat']
    when Doller
      ['doller']
    end
  end
  [*compile_r.(pattern), 'match']
end

def scan(code, string)
  from = 0
  arr = []
  loop do
    last_vm = exec(code, string, from)
    break if last_vm.nil?

    range = last_vm[:sp_from]..last_vm[:sp]
    arr << if range.begin > range.end
             from = last_vm[:sp_from] + 1
             STDERR.puts 'maybe bug'
             nil
           else
             from = last_vm[:sp] + 1
             last_vm
           end
  end
  arr.compact
end

def exec(init_codes, string, from = 0)
  # sp: 0,1,2... から始めることで、前方部分一致をサポート
  init_frames = (from..(string.size - 1)).map do |i|
    { codes: init_codes, sp: i, pc: 0, sp_from: i, skips: [] }
  end.reverse

  loop.reduce(init_frames) do |frames, _|
    break nil if frames.empty?

    f = frames.last
    frames_tail = frames[0..-2]

    sp      = f[:sp] # sp: string pointer
    pc      = f[:pc]
    sp_from = f[:sp_from]
    skips   = f[:skips]
    codes   = f[:codes]
    code    = codes[pc]

    if $debug
      puts
      p frames, code
    end

    case code
    when /^char (.*)/
      method = string[sp]&.name
      p method if $debug
      next frames_tail if method.nil?

      c = $1
      skip = nil
      if c.start_with?('\\')
        c = c.sub('\\', '')
        skip = sp
      end

      if method == c || c == '.'
        next [*frames_tail, { **f, sp: sp + 1, pc: pc + 1, skips: [*skips, skip].compact }]
      else
        next frames_tail
      end
    when /^hat/
      if sp == 0
        next [*frames_tail, { **f, pc: pc + 1 }]
      else
        next frames_tail
      end
    when /^doller/
      if sp == string.size
        next [*frames_tail, { **f, pc: pc + 1 }]
      else
        next frames_tail
      end
    when /^split (-?\d+) (-?\d+)/
      j1 = $1.to_i
      j2 = $2.to_i
      next [
        *frames_tail,
        { **f, pc: pc + 1  },
        { **f, pc: pc + j2 },
        { **f, pc: pc + j1 } # 最長一致のため、繰り返す方(j1)を先に試す
      ]
    when /^jump (-?\d+)/
      j = $1.to_i
      next [*frames_tail, { **f, pc: pc + j }]
    when /^ref_from (\d)/
      next [*frames_tail, { **f, pc: pc + 1, "ref_from_#{$1}": sp }]
    when /^ref_to (\d)/
      next [*frames_tail, { **f, pc: pc + 1, "ref_to_#{$1}": sp - 1 }]
    when /^backref (\d)/
      chars = string[f[:"ref_from_#{$1}"]..f[:"ref_to_#{$1}"]]
                    .map(&:name)
      # jumpで戻ってくる時の位置の計算が合うように、charの連続ではなく1個のcharsに置き換えている
      next [
        *frames_tail, {
          **f,
          pc: pc,
          codes: [
            *codes[0..(pc-1)],
            "chars #{chars.join(' ')}",
            *codes[(pc+1)..-1]
          ]
        }
      ]
    when /chars (.*)/
      chars = $1.split.map { |c| c.sub('\\', '') }
      methods = string[sp..nil].take(chars.size).map(&:name).compact
      next frames_tail if methods.size < chars.size

      add_skips = []
      methods = methods.map.with_index do |method, i|
        if method.start_with?('\\')
          add_skips << sp + i
          method.sub('\\', '')
        else
          method
        end
      end

      if chars == methods
        next [*frames_tail, { **f, sp: sp + 1, pc: pc + 1, skips: [*skips, *add_skips] }]
      else
        next frames_tail
      end

    when /^match/
      break { **f, sp: sp - 1 }
    end
  end
end

if $PROGRAM_NAME == __FILE__
  $debug = true
  puts run(ARGV[0], ARGV[1])
end
