drop database if exists tnp;
create database tnp;

GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'root' WITH GRANT OPTION;
FLUSH PRIVILEGES;

use tnp;

create table sensors(	sensor_id integer NOT NULL AUTO_INCREMENT, ip varchar(15) not null, active boolean not null, start DATETIME not null, end DATETIME, primary key(sensor_id)) engine = InnoDB;
create table rtts(	rtt_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, min double not null, max double not null, avg double not null, dev double not null, time DATETIME not null, primary key(rtt_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table bw(	bw_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, bytes integer NOT NULL, duration double NOT NULL, speed integer NOT NULL, time DATETIME NOT NULL, primary key(bw_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table udps(	udp_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, size integer NOT NULL, duration double NOT NULL, dscp_flag integer NOT NULL, send_bw integer NOT NULL, bw integer NOT NULL, jitter integer NOT NULL, packet_loss integer NOT NULL, time DATETIME NOT NULL, primary key(udp_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;	