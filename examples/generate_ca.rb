#!/usr/bin/env mruby
# This script can be run directly thanks to the shebang capability

def options()
  program_name = File.basename __FILE__

  matches = Clap.parse(program_name) do |cmd|
    cmd.about "Generate a root CA for your PKI"
    cmd.long_about <<~EOF
      This command will generate a self signed certificate with a name constraint on DOMAIN (default to: example.com). It is inspired by the work from: https://systemoverlord.com/2020/06/14/private-ca-with-x-509-name-constraints.html

      Example:

        $ #{program_name} --days 365 --clean foo.bar
    EOF

    cmd.arg "days" do |a|
      a.short "D"
      a.long "days"
      a.help "Set validity period of root CA"
      a.default "3650"
      a.int
      a.value_name "N"
    end

    cmd.arg "clean" do |a|
      a.long "clean"
      a.help "Clean intermediate files, will re-generate certificate each time"
      a.flag
    end

    cmd.arg "domain" do |a|
      a.positional
      a.help "Domain name for the certificate"
      a.default "example.com"
      a.value_name "DOMAIN"
    end
  end

  {
    domain: matches.get_one("domain"),
    days: matches.get_one("days", :int),
    clean: matches.flag?("clean"),
  }
end

# Certificate extends the Rake DSL, enabling the use and invocation of its functions
# and keywords within code. This serves as an example of how to leverage the DSL
# programmatically, without the need to write a Rakefile.
class Certificate
  include Rake::DSL

  def initialize(domain, days, clean)
    domain_slug = domain.to_s.gsub("\.", "_")
    key = "#{domain_slug}-#{days}-key.pem"
    conf = "#{domain_slug}-conf.ini"
    cert = "#{domain_slug}-#{days}-rootCA.pem"
    req = "#{domain_slug}-#{days}-crt.pem"

    file key do |t|
      sh "openssl genpkey -algorithm ed25519 -out #{t.name}"
    end
    file conf do |t|
      puts "generate config file"
      # inline file creation with a template using heredoc
      File.open(t.name, File::CREAT | File::TRUNC | File::WRONLY).write(<<~EOF
        basicConstraints = critical, CA:TRUE
        keyUsage = critical, keyCertSign, cRLSign
        subjectKeyIdentifier = hash
        nameConstraints = critical, permitted;DNS:.#{domain}
      EOF
      )
    end
    file req => [key] do |t|
      sh "openssl req -new -key #{t.prerequisites.first} -extensions v3_ca -batch -out #{t.name} -utf8"
    end

    @cert = file cert => [key, req, conf] do |t|
      sh "openssl x509 -req -sha256 -days #{days} -in #{req} -signkey #{key} -extfile #{conf} -out #{t.name}"
      next unless clean # exit early if we don't need to clean up
      File.delete(req)
      File.delete(conf)
    end
  end

  # This function is our entrypoint and invoke the Rake task "cert"
  def generate = @cert.invoke()

  # Let's use a static function and take advantage of ruby syntax
  def self.generate(domain:, days:, clean:) = Certificate.new(domain, days, clean).generate
end

Certificate.generate(**options)
