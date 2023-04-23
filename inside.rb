class Inside
  @methods = []
  @enable = false

  TP_CALL = TracePoint.trace(:call) do |tp|
    next unless @methods.any?([tp.self.class, tp.method_id])

    tracepoint_call
    @enable = true
  end

  TP_B_CALL = TracePoint.trace(:b_call) do |tp|
    next unless @enable

    tracepoint_b_call
  end

  TP_RETURN = TracePoint.trace(:return) do |tp|
    next unless @methods.any?([tp.self.class, tp.method_id])

    @enable = false
  end

  class << self
    def add(klass, method)
      @methods << [klass, method]
    end

    def delete(klass, method)
      @methods.detele(klass, method)
    end
  end
end

class Module
  def inside(method)
    Inside.add(self, method.name.to_sym)
  end
end

# class Array
#   inside def my_filter
#     filter { yield(_1) }
#   end
# end
# [1,2,3].my_filter { p self }


