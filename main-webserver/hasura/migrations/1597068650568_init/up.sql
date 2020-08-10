CREATE SCHEMA devops;
CREATE SCHEMA hardware;
CREATE SCHEMA logs;
CREATE SCHEMA sales;
CREATE TABLE devops.alembic_version (
    version_num character varying
);
CREATE TABLE devops.release_groups (
    release_stage integer NOT NULL,
    branch character varying(50) NOT NULL
);
CREATE TABLE hardware.apps_to_install (
    disk_id character varying(250) NOT NULL,
    user_id character varying(250) NOT NULL,
    app_id character varying(50) NOT NULL
);
CREATE TABLE hardware.install_commands (
    install_command_id integer NOT NULL,
    windows_install_command text,
    linux_install_command text,
    app_name text
);
ALTER TABLE hardware.install_commands ALTER COLUMN install_command_id ADD GENERATED BY DEFAULT AS IDENTITY (
    SEQUENCE NAME hardware.install_commands_install_command_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);
CREATE TABLE hardware.os_disks (
    disk_id character varying(250) NOT NULL,
    location character varying(50) NOT NULL,
    os character varying(50) NOT NULL,
    disk_size integer NOT NULL,
    allow_autoupdate boolean NOT NULL,
    has_dedicated_vm boolean NOT NULL,
    version character varying(50),
    rsa_private_key character varying(250),
    using_stun boolean NOT NULL,
    ssh_password character varying(50),
    user_id integer,
    last_pinged integer,
    branch character varying(50) DEFAULT 'master'::character varying NOT NULL
);
CREATE TABLE hardware.secondary_disks (
    disk_id character varying(250) NOT NULL,
    user_id character varying(250) NOT NULL,
    os character varying(50) NOT NULL,
    disk_size integer NOT NULL,
    location character varying(50) NOT NULL
);
CREATE TABLE hardware.user_vms (
    vm_id character varying(250) NOT NULL,
    ip character varying(50) NOT NULL,
    location character varying(50) NOT NULL,
    os character varying(50) NOT NULL,
    state character varying(50) NOT NULL,
    lock boolean NOT NULL,
    user_id integer,
    temporary_lock integer
);
CREATE TABLE logs.monitor_logs (
    logons integer NOT NULL,
    logoffs integer NOT NULL,
    users_online integer NOT NULL,
    eastus_available integer NOT NULL,
    southcentralus_available integer NOT NULL,
    northcentralus_available integer NOT NULL,
    eastus_unavailable integer NOT NULL,
    southcentralus_unavailable integer NOT NULL,
    northcentralus_unavailable integer NOT NULL,
    eastus_deallocated integer NOT NULL,
    southcentralus_deallocated integer NOT NULL,
    northcentralus_deallocated integer NOT NULL,
    total_vms_deallocated integer NOT NULL,
    "timestamp" integer
);
CREATE TABLE logs.protocol_logs (
    user_id character varying(250),
    server_logs character varying(250),
    connection_id character varying(50) NOT NULL,
    bookmarked boolean NOT NULL,
    "timestamp" integer,
    version character varying(50),
    client_logs character varying(250)
);
CREATE TABLE public.users (
    user_id integer NOT NULL,
    email character varying(250) NOT NULL,
    name character varying(250),
    password character varying(250) NOT NULL,
    release_stage integer NOT NULL,
    stripe_customer_id character varying(250),
    created_timestamp integer,
    reason_for_signup text,
    referral_code character varying(50),
    credits_outstanding integer DEFAULT 0,
    using_google_login boolean DEFAULT false
);
ALTER TABLE public.users ALTER COLUMN user_id ADD GENERATED BY DEFAULT AS IDENTITY (
    SEQUENCE NAME public.users_user_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1
);
CREATE TABLE sales.main_newsletter (
    user_id character varying(250) NOT NULL
);
CREATE TABLE sales.stripe_products (
    stripe_product_id character varying(50) NOT NULL,
    product_name character varying(250) NOT NULL
);
ALTER TABLE ONLY hardware.apps_to_install
    ADD CONSTRAINT "PK_apps_to_install" PRIMARY KEY (disk_id, user_id);
ALTER TABLE ONLY hardware.secondary_disks
    ADD CONSTRAINT "PK_secondary_disks" PRIMARY KEY (disk_id, user_id);
ALTER TABLE ONLY hardware.user_vms
    ADD CONSTRAINT "PK_user_vms" PRIMARY KEY (vm_id);
ALTER TABLE ONLY hardware.os_disks
    ADD CONSTRAINT unique_disk_id UNIQUE (disk_id);
ALTER TABLE ONLY hardware.user_vms
    ADD CONSTRAINT unique_vm_id UNIQUE (vm_id);
ALTER TABLE ONLY public.users
    ADD CONSTRAINT "PK_users" PRIMARY KEY (user_id);
ALTER TABLE ONLY public.users
    ADD CONSTRAINT unique_email UNIQUE (email);
ALTER TABLE ONLY sales.stripe_products
    ADD CONSTRAINT "PK_stripe_products" PRIMARY KEY (stripe_product_id);
ALTER TABLE ONLY sales.main_newsletter
    ADD CONSTRAINT unique_user_id UNIQUE (user_id);
CREATE INDEX "fkIdx_109" ON hardware.secondary_disks USING btree (user_id);
CREATE INDEX "fkIdx_115" ON hardware.apps_to_install USING btree (disk_id, user_id);
CREATE INDEX "fkIdx_29" ON hardware.apps_to_install USING btree (app_id);
CREATE INDEX "fkIdx_61" ON logs.protocol_logs USING btree (user_id);
CREATE INDEX "fkIdx_91" ON sales.main_newsletter USING btree (user_id);
ALTER TABLE ONLY hardware.user_vms
    ADD CONSTRAINT fk_user_id FOREIGN KEY (user_id) REFERENCES public.users(user_id);
ALTER TABLE ONLY hardware.os_disks
    ADD CONSTRAINT fk_user_id FOREIGN KEY (user_id) REFERENCES public.users(user_id);
