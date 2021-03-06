-- This schema uses Atom 1.0 nomenclature --
begin;

pragma user_version = 3;

create table "feeds" (
  "id"          integer NOT NULL PRIMARY KEY,
  "title"       text
);
  
create table "entries" (
  "id"          integer NOT NULL PRIMARY KEY,
  "full_id"     text,
  "updated"     real,
  "feed_id"     integer NOT NULL,
  "created_at"  real,
  constraint "entry_feed" foreign key ("feed_id")
    references "feeds" ("id") ON DELETE CASCADE
);
  
create table "tokens" (
  "id"       integer NOT NULL PRIMARY KEY,
  "token"    text    NOT NULL UNIQUE
);
  
create table "random_backgrounds" (
  "entry_id" integer NOT NULL PRIMARY KEY,
  constraint "random_backgrounds_entry_id" foreign key ("entry_id")
    references "entries" ("id")
);

CREATE INDEX IF NOT EXISTS entry_updated on entries(updated);

-- Prevent insertion of entries without feeds
CREATE TRIGGER entry_feed_insert
  BEFORE INSERT ON entries
  FOR EACH ROW BEGIN
      SELECT RAISE(ROLLBACK, 'insert on table "entries" violates foreign key constraint "entry_feed"')
      WHERE  NEW.feed_id IS NOT NULL
             AND (SELECT id FROM feeds WHERE id = new.feed_id) IS NULL;
  END;
  
-- Cascade delete from feeds to entries  
create trigger entry_feed_delete
  BEFORE DELETE ON feeds
  FOR EACH ROW BEGIN
      DELETE from entries WHERE feed_id = OLD.id;
  END;
  
-- Prevent deletion of items that are in the random background  
CREATE TRIGGER random_backgrounds_entry_id
  BEFORE DELETE ON entries
  FOR EACH ROW BEGIN
      SELECT RAISE(ROLLBACK, 'delete on table "entries" violates foreign key constraint "random_backgrounds_entry_id"')
      WHERE (SELECT entry_id FROM random_backgrounds WHERE entry_id = OLD.id) IS NOT NULL;
  END;
  
commit;
