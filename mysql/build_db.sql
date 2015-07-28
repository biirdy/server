drop database if exists sensor_data;
create database sensor_data;

GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'root' WITH GRANT OPTION;
FLUSH PRIVILEGES;

use tnp;

create table sensors(		sensor_id integer NOT NULL AUTO_INCREMENT, ether varchar(17) NOT NULL, ip varchar(15) NOT NULL, loacl_ip varchar(15) NOT NULL, active boolean not null, start DATETIME not null, end DATETIME, description varchar(50), UNIQUE(ether), primary key(sensor_id)) engine = InnoDB; #make ether unique!!
create table rtts(			rtt_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, dst_id integer NOT NULL, min double not null, max double not null, avg double not null, dev double not null, time DATETIME not null, primary key(rtt_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table bw(			bw_id integer NOT NULL AUTO_INCREMENT, 	sensor_id integer NOT NULL, dst_id integer NOT NULL, bytes integer NOT NULL, duration double NOT NULL, speed integer NOT NULL, time DATETIME NOT NULL, primary key(bw_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table udps(			udp_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, dst_id integer NOT NULL, size integer NOT NULL, duration double NOT NULL, dscp_flag integer NOT NULL, send_bw double NOT NULL, bw double NOT NULL, jitter double NOT NULL, packet_loss double NOT NULL, time DATETIME NOT NULL, primary key(udp_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;	
create table dns(			dns_id integer NOT NULL AUTO_INCREMENT, dst_id integer NOT NULL, duration integer NOT NULL, time DATETIME NOT NULL, primary key(dns_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table dns_failure(	dns_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, time DATETIME NOT NULL, primary key(dns_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;

create table schedules(		schedule_id integer NOT NULL AUTO_INCREMENT, recipient integer NOT NULL, sensor integer NOT NULL, period integer NOT NULL description varchar(50), primary key(schedule_id), foreign key(recipient) references sensors(sensor_id), foreign key(sensor) references sensors(sensor_id)) engine = InnoDB;
create table schdule_params( param_id integer NOT NULL AUTO_INCREMENT, schedule_id integer NOT NULL, param varchar(10) NOT NULL, value integer NOT NULL, primary key(param_id), foreign key(schedule_id) references schedules(schedule_id)) engine = InnoDB;
alter table schedule_params add unique index(param, schedule_id);

drop database if exists members;
create database members;

use members;

CREATE TABLE `members` ( `id` int(4) NOT NULL auto_increment, `username` varchar(65) NOT NULL default '', `password` varchar(65) NOT NULL default '', PRIMARY KEY (`id`) ) engine = InnoDB;


