Name:           dsr-uu
Version:        0.9.1
Release:        1
Summary:        An source routed routing protocol for ad hoc networks.

Group:          System Environment/Base
License:        GPL
Source:         dsr-uu-0.1.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%{!?kernel: %{expand: %%define        kernel          %(uname -r)}}

%define       kversion        %(echo %{kernel} | sed -e s/smp// -)
%define       krelver         %(echo %{kversion} | tr -s '-' '_')

%if %(echo %{kernel} | grep -c smp)
        %{expand:%%define ksmp -smp}
%endif

%description 

DSR (Dynamic Source Routing) is a routing protocol for ad hoc
networks. It uses source routing and is being developed within the
MANET Working Group of the IETF.

%prep
%setup -q

%build
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS"

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/sbin
mkdir -p $RPM_BUILD_ROOT/lib/modules/%{kernel}/dsr-uu

install -s -m 755 aodvd $RPM_BUILD_ROOT/usr/sbin/aodvd        
install -m 644 dsr.ko $RPM_BUILD_ROOT/lib/modules/%{kernel}/dsr-uu/dsr.ko
install -m 644 linkcache.ko $RPM_BUILD_ROOT/lib/modules/%{kernel}/dsr-uu/linkcache.ko



%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/depmod -a

%postun
/sbin/depmod -a

%files
%defattr(-,root,root)
%doc README README.ns ChangeLog

/usr/sbin/aodvd
/lib/modules/%{kernel}/aodv

%changelog
* Wed Jul 27 2005 Erik Nordström <erikn@replicator.mine.nu> - 0.9.1-1
- First spec file
