<?php

/*
    Copyright (C) 2014-2017 Franco Fichtner <franco@opnsense.org>
    Copyright (C) 2010 Ermal Luçi
    Copyright (C) 2005-2006 Colin Smith <ethethlay@gmail.com>
    Copyright (C) 2003-2004 Manuel Kasper <mk@neon1.net>
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
    AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
    OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

function dnsmasq_enabled()
{
    global $config;

    return isset($config['dnsmasq']['enable']);
}

function dnsmasq_configure()
{
    return array(
        'dns' => array('dnsmasq_configure_do'),
        'hosts' => array('dnsmasq_hosts_generate:0'),
    );
}

function dnsmasq_services()
{
    $services = array();

    if (!dnsmasq_enabled()) {
        return $services;
    }

    $pconfig = array();
    $pconfig['name'] = 'dnsmasq';
    $pconfig['description'] = gettext('Dnsmasq DNS');
    $pconfig['php']['restart'] = array('dnsmasq_configure_do');
    $pconfig['php']['start'] = array('dnsmasq_configure_do');
    $pconfig['pidfile'] = '/var/run/dnsmasq.pid';
    $services[] = $pconfig;

    return $services;
}

function dnsmasq_syslog()
{
    $logfacilities = array();

    $logfacilities['dnsmasq'] = array('facility' => array('dnsmasq'), 'remote' => 'dns');

    return $logfacilities;
}

function dnsmasq_xmlrpc_sync()
{
    $result = array();

    $result[] = array(
        'description' => gettext('Dnsmasq DNS'),
        'section' => 'dnsmasq',
        'id' => 'dnsforwarder',
    );

    return $result;
}

function dnsmasq_configure_do($verbose = false)
{
    global $config;

    // hard coded args: will be removed to avoid duplication if specified in custom_options
    $standard_args = array(
        "dns-forward-max" => "--dns-forward-max=5000",
        "cache-size" => "--cache-size=10000",
        "local-ttl" => "--local-ttl=1"
    );

    _dnsmasq_dhcpleases_stop();

    killbypid('/var/run/dnsmasq.pid', 'TERM', true);

    if (!dnsmasq_enabled()) {
        return;
    }

    if ($verbose) {
        echo 'Starting Dnsmasq DNS...';
        flush();
    }

    $args = ' -H /var/etc/dnsmasq-hosts ';

    /* Setup listen port, if non-default */
    if (isset($config['dnsmasq']['port']) && is_port($config['dnsmasq']['port'])) {
        $args .= " --port={$config['dnsmasq']['port']} ";
    }

    if (isset($config['dnsmasq']['interface'])) {
        $addresses = array();
        foreach (explode(",", $config['dnsmasq']['interface']) as $interface) {
            if (is_ipaddrv4($interface)) {
                $addresses[] = $interface;
            } elseif (is_ipaddrv6($interface)) {
                 // Since dnsmasq does not support link-local address with scope specified. strip address.
                 $addresses[] = explode("%", $interface)[0];
            } else {
                $intf_ipv4 = get_interface_ip($interface);
                $intf_ipv6 = get_interface_ipv6($interface);
                if (!empty($intf_ipv4)) {
                    $addresses[] = $intf_ipv4;
                }
                if (!empty($intf_ipv6)) {
                    $addresses[] = explode("%", $intf_ipv6)[0];
                }
            }
        }
        foreach ($addresses as $address) {
            $args .= " --listen-address={$address} ";
        }
        if (!empty($addresses) && isset($config['dnsmasq']['strictbind'])) {
            $args .= " --bind-interfaces ";
        }
    }

    /* Prevent bogus lookups of unknown private ranges */
    if (isset($config['dnsmasq']['no_private_reverse'])) {
        $args .= " --bogus-priv ";
    }

    /* Setup forwarded domains */
    if (isset($config['dnsmasq']['domainoverrides']) && is_array($config['dnsmasq']['domainoverrides'])) {
      foreach($config['dnsmasq']['domainoverrides'] as $override) {
        if ($override['ip'] == "!") {
            $override['ip'] = "";
        }
        $args .= ' --server='. escapeshellarg('/' . $override['domain'] . '/' . $override['ip']);
      }
    }

    /* Allow DNS Rebind for forwarded domains */
    if (isset($config['dnsmasq']['domainoverrides']) && is_array($config['dnsmasq']['domainoverrides'])) {
        if (!isset($config['system']['webgui']['nodnsrebindcheck'])) {
            foreach($config['dnsmasq']['domainoverrides'] as $override) {
                $args .= ' --rebind-domain-ok=' . escapeshellarg('/'.$override['domain'].'/') . ' ' ;
            }
        }
    }

    if (!isset($config['system']['webgui']['nodnsrebindcheck'])) {
        $dns_rebind = "--rebind-localhost-ok --stop-dns-rebind";
    }

    if (isset($config['dnsmasq']['strict_order'])) {
        $args .= " --strict-order ";
    }

    if (isset($config['dnsmasq']['domain_needed'])) {
        $args .= " --domain-needed ";
    }

    if (isset($config['dnsmasq']['custom_options']) && !empty($config['dnsmasq']['custom_options'])) {
        foreach (preg_split('/\s+/', $config['dnsmasq']['custom_options']) as $c) {
            $args .= " " . escapeshellarg("--{$c}");
            $p = explode('=', $c);
            if (array_key_exists($p[0], $standard_args)) {
                unset($standard_args[$p[0]]);
            }
        }
    }
    $args .= ' ' . implode(' ', array_values($standard_args));

    _dnsmasq_add_host_entries();

    mwexec("/usr/local/sbin/dnsmasq --all-servers {$dns_rebind} {$args}");

    _dnsmasq_dhcpleases_start();

    if ($verbose) {
        echo "done.\n";
    }
}

function _dnsmasq_add_host_entries()
{
    global $config;

    $dnsmasqcfg = $config['dnsmasq'];
    $lhosts = '';
    $dhosts = '';

    if (!isset($dnsmasqcfg['hosts']) || !is_array($dnsmasqcfg['hosts'])) {
        $dnsmasqcfg['hosts'] = array();
    }

    foreach ($dnsmasqcfg['hosts'] as $host) {
        if ($host['host']) {
            $lhosts .= "{$host['ip']}\t{$host['host']}.{$host['domain']} {$host['host']}\n";
        } else {
            $lhosts .= "{$host['ip']}\t{$host['domain']}\n";
        }
        if (!isset($host['aliases']) || !is_array($host['aliases']) || !is_array($host['aliases']['item'])) {
            continue;
        }
        foreach ($host['aliases']['item'] as $alias) {
            if ($alias['host']) {
                $lhosts .= "{$host['ip']}\t{$alias['host']}.{$alias['domain']} {$alias['host']}\n";
            } else {
                $lhosts .= "{$host['ip']}\t{$alias['domain']}\n";
            }
        }
    }

    if (isset($dnsmasqcfg['regdhcpstatic']) && is_array($config['dhcpd'])) {
        foreach ($config['dhcpd'] as $dhcpif => $dhcpifconf) {
            if (isset($dhcpifconf['staticmap']) && isset($dhcpifconf['enable'])) {
                foreach ($dhcpifconf['staticmap'] as $host) {
                    if (!$host['ipaddr'] || !$host['hostname']) {
                        continue;
                    }

                    $domain = $config['system']['domain'];
                    if ($host['domain']) {
                        $domain = $host['domain'];
                    } elseif ($dhcpifconf['domain']) {
                        $domain = $dhcpifconf['domain'];
                    }

                    $dhosts .= "{$host['ipaddr']}\t{$host['hostname']}.{$domain} {$host['hostname']}\n";
                }
            }
        }
    }

    if (isset($dnsmasqcfg['regdhcpstatic']) && is_array($config['dhcpdv6'])) {
        foreach ($config['dhcpdv6'] as $dhcpif => $dhcpifconf) {
            if (isset($dhcpifconf['staticmap']) && isset($dhcpifconf['enable'])) {
                foreach ($dhcpifconf['staticmap'] as $host) {
                    if (!$host['ipaddrv6'] || !$host['hostname']) {
                        continue;
                    }

                    $domain = $config['system']['domain'];
                    if ($host['domain']) {
                        $domain = $host['domain'];
                    } elseif ($dhcpifconf['domain']) {
                        $domain = $dhcpifconf['domain'];
                    }

                    $dhosts .= "{$host['ipaddrv6']}\t{$host['hostname']}.{$domain} {$host['hostname']}\n";
                }
            }
        }
    }

    if (isset($dnsmasqcfg['dhcpfirst'])) {
        file_put_contents('/var/etc/dnsmasq-hosts', $dhosts . $lhosts);
    } else {
        file_put_contents('/var/etc/dnsmasq-hosts', $lhosts . $dhosts);
    }

    /*
     * Several reports indicated 600 permissions on this file.
     * The reason is currently unknown, but setting it to 644
     * as it should be brings the service back to life.
     */
    chmod('/var/etc/dnsmasq-hosts', 0644);
}

function _dnsmasq_dhcpleases_start()
{
    global $config;

    $leases = services_dhcpd_leasesfile();

    if (isset($config['dnsmasq']['regdhcp']) && file_exists($leases)) {
        $domain = $config['system']['domain'];
        if (isset($config['dnsmasq']['regdhcpdomain'])) {
            $domain = $config['dnsmasq']['regdhcpdomain'];
        }
        mwexecf(
            '/usr/local/sbin/dhcpleases -l %s -d %s -p %s -h %s',
            array($leases, $domain, '/var/run/dnsmasq.pid', '/var/etc/dnsmasq-hosts')
        );
    }
}

function _dnsmasq_dhcpleases_stop()
{
    killbypid('/var/run/dhcpleases.pid', 'TERM', true);
}

function dnsmasq_hosts_generate()
{
    _dnsmasq_dhcpleases_stop();

    if (!dnsmasq_enabled()) {
        return;
    }

    _dnsmasq_add_host_entries();
    _dnsmasq_dhcpleases_start();

    killbypid('/var/run/dnsmasq.pid', 'HUP');
}
