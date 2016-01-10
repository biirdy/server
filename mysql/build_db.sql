drop database if exists weperf_data;
create database weperf_data;

GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'root' WITH GRANT OPTION;
FLUSH PRIVILEGES;

use weperf_data;

#sensors
create table sensors(		sensor_id integer NOT NULL AUTO_INCREMENT, ether varchar(17) NOT NULL, ip varchar(15) NOT NULL, loacl_ip varchar(15) NOT NULL, active boolean not null, start DATETIME not null, end DATETIME, description varchar(50), UNIQUE(ether), primary key(sensor_id)) engine = InnoDB; 

#metrics
create table rtt(			rtt_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, dst_id integer NOT NULL, measurement_id integer NOT NULL, min double not null, max double not null, avg double not null, dev double not null, time DATETIME not null, primary key(rtt_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table tcp(			bw_id integer NOT NULL AUTO_INCREMENT, 	sensor_id integer NOT NULL, dst_id integer NOT NULL, measurement_id integer NOT NULL, bytes integer NOT NULL, duration double NOT NULL, speed integer NOT NULL, time DATETIME NOT NULL, primary key(bw_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table udp(			udp_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, dst_id integer NOT NULL, measurement_id integer NOT NULL, size integer NOT NULL, duration double NOT NULL, dscp_flag integer NOT NULL, send_bw double NOT NULL, bw double NOT NULL, jitter double NOT NULL, packet_loss double NOT NULL, time DATETIME NOT NULL, primary key(udp_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;	
create table dns(			dns_id integer NOT NULL AUTO_INCREMENT, dst_id integer NOT NULL, duration integer NOT NULL, time DATETIME NOT NULL, primary key(dns_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
create table dns_failure(	dns_id integer NOT NULL AUTO_INCREMENT, sensor_id integer NOT NULL, time DATETIME NOT NULL, primary key(dns_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;

#schedules
create table schedules(				schedule_id integer NOT NULL AUTO_INCREMENT, name varchar(10) NOT NULL, description varchar(80), period integer NOT NULL, active integer NOT NULL, primary key(schedule_id)) engine = InnoDB;
create table schedule_measurements(	measurement_id integer NOT NULL AUTO_INCREMENT, schedule_id integer NOT NULL, pid integer NOT NULL, destination_id integer NOT NULL, source_id integer NOT NULL, destination_type integer NOT NULL, source_type integer NOT NULL, delay integer NOT NULL, method varchar(10), active integer NOT NULL, status integer NOT NULL, primary key(measurement_id)) engine = InnoDB;
create table schdule_params( 		param_id integer NOT NULL AUTO_INCREMENT, measurement_id integer NOT NULL, param varchar(10) NOT NULL, value integer NOT NULL, primary key(param_id), foreign key(measurement_id) references schedule_measurements(measurement_id)) engine = InnoDB;
alter table schedule_params add unique index(param, schedule_id);

#create default scheudles/measurements
insert into schedules(				schedule_id, name, description, period, active) VALUES(1, 'Default', 'Defult measurements between all sensors and server', 3600, 0);
insert into schedule_measurements(	measurement_id, schedule_id, pid, source_id, source_type, destination_id, destination_type, delay, method, active, status) VALUES(1, 1, 0, 1, 1, 1, 0, 10, "rtt", 0, 0);
insert into schedule_measurements(	measurement_id, schedule_id, pid, source_id, source_type, destination_id, destination_type, delay, method, active, status) VALUES(2, 1, 0, 1, 1, 1, 0, 10, "tcp", 0, 0);
insert into schedule_measurements(	measurement_id, schedule_id, pid, source_id, source_type, destination_id, destination_type, delay, method, active, status) VALUES(3, 1, 0, 1, 1, 1, 0, 10, "dns", 0, 0);
insert into schedule_params(		measurement_id, param, value) VALUES(1, 'iterations', '5');
insert into schedule_params(		measurement_id, param, value) VALUES(2, 'duration', '5');
insert into schedule_params(		measurement_id, param, value) VALUES(3, 'domain_name', 'google.co.uk');
insert into schedule_params(		measurement_id, param, value) VALUES(3, 'server', 'default');	

#groups
create table groups(			group_id integer NOT NULL AUTO_INCREMENT, name varchar(20) NOT NULL, description varchar(50), num_sensors integer NOT NULL, primary key(group_id)) engine = InnoDB;
create table group_membership(	id integer NOT NULL AUTO_INCREMENT, group_id integer NOT NULL, sensor_id integer NOT NULL, primary key(id), foreign key(group_id) references groups(group_id), foreign key(sensor_id) references sensors(sensor_id)) engine = InnoDB;
alter table group_membership add unique index(group_id, sensor_id);

#create defulat group

#alarms

#members
drop database if exists weperf_users;
create database weperf_users;

use weperf_users;

CREATE TABLE users(id int(4) NOT NULL auto_increment, username varchar(65) NOT NULL default '', email varchar(65) NOT NULL default '', password varchar(65) NOT NULL default '', admin boolean NOT NULL default false, PRIMARY KEY(id)) engine = InnoDB;

#create defult memeber
insert into members(username, email, admin, password) VALUES('admin', 'admin@admin.net', true, 'admin',);

