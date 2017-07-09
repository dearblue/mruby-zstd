#!ruby

MRUBY_CONFIG ||= ENV["MRUBY_CONFIG"] || "test_config.rb"
ENV["MRUBY_CONFIG"] = MRUBY_CONFIG

Object.instance_eval { remove_const(:MRUBY_CONFIG) }

MRUBY_BASEDIR ||= ENV["MRUBY_BASEDIR"] || "@mruby"

load "#{MRUBY_BASEDIR}/Rakefile"
