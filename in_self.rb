$with_self_methods = []

TracePoint.trace(:call) do |tp|
  next unless $with_self_methods.any?(tp.method_id)

  tracepoint_call
  $fire_b_call_hook = true
end

TracePoint.trace(:return) do |tp|
  next unless $with_self_methods.any?(tp.method_id)

  $fire_b_call_hook = false
end

TracePoint.trace(:b_call) do |tp|
  tracepoint_b_call if $fire_b_call_hook
end


class WithProxy
  def initialize(slf)
    @slf = slf
  end

  def method_missing(name, *args, &block)
    with_self_method = :"withself_#{name}"
    $with_self_methods << with_self_method

    eval <<-EOS
      def @slf.#{with_self_method}(&b)
        #{name} { |a| b.(a) }
      end
    EOS

    @slf.send(with_self_method, *args, &block)
  end
end

class Object
  def with
    WithProxy.new(self)
  end
end

